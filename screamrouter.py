#!/usr/bin/python3
"""ScreamRouter"""
import multiprocessing
import os
import signal
import sys
import threading
import pyximport

import uvicorn
from fastapi import FastAPI

import src.constants.constants as constants
from src.api.api_configuration import APIConfiguration
from src.api.api_website import APIWebsite
from src.api.api_webstream import APIWebStream
from src.configuration.configuration_manager import ConfigurationManager
from src.plugin_manager.plugin_manager import PluginManager
from src.screamrouter_logger.screamrouter_logger import get_logger
from src.utils.utils import set_process_name

pyximport.install()

os.nice(-15)

logger = get_logger(__name__)

threading.current_thread().name = "ScreamRouter Main Thread"
main_pid: int = os.getpid()
ctrl_c_pressed: int = 0

def signal_handler(_signal, __):
    """Fired when Ctrl+C pressed"""
    if _signal == 2:
        if os.getpid() != main_pid:
            logger.error("Ctrl+C on non-main PID %s", os.getpid())
            return
        logger.error("Ctrl+C pressed")
        website.stop()
        try:
            screamrouter_configuration.stop()
        except NameError:
            pass
        server.should_exit = True
        server.force_exit = True
        os.kill(os.getpid(), signal.SIGTERM)
        sys.exit(0)


signal.signal(signal.SIGINT, signal_handler)

app: FastAPI = FastAPI( title="ScreamRouter",
        description = "Routes PCM audio around for Scream sinks and sources",
        version="0.0.1",
        contact={
            "name": "ScreamRouter",
            "url": "http://github.com/netham45/screamrouter",
        },
        license_info={
            "name": "No license chosen yet, all rights reserved",
        },
        openapi_tags=[
        {
            "name": "Sink Configuration",
            "description": "API endpoints for managing Sinks"
        },
        {
            "name": "Source Configuration",
            "description": "API endpoints for managing Sources"
        },
        {
            "name": "Route Configuration",
            "description": "API endpoints for managing Routes"
        },
        {
            "name": "Site",
            "description": "File handlers for the site interface"
        },
        {
            "name": "Stream",
            "description": "HTTP media streams"
        }
    ])
set_process_name("SR Scream Router", "Scream Router Main Thread")
if constants.DEBUG_MULTIPROCESSING:
    logger = multiprocessing.log_to_stderr()
    logger.setLevel(multiprocessing.SUBDEBUG) # type: ignore
webstream: APIWebStream = APIWebStream(app)
plugin_manager: PluginManager = PluginManager(app)
plugin_manager.start_registered_plugins()
screamrouter_configuration: ConfigurationManager = ConfigurationManager(webstream, plugin_manager)
api_controller = APIConfiguration(app, screamrouter_configuration)
website: APIWebsite = APIWebsite(app, screamrouter_configuration)

config = uvicorn.Config(app=app,
                        port=constants.API_PORT,
                        host=constants.API_HOST,
                        log_config="uvicorn_log_config.yaml" if constants.LOG_TO_FILE else None,
                        timeout_keep_alive=30,
                        ssl_keyfile=constants.CERTIFICATE_KEY,
                        ssl_certfile=constants.CERTIFICATE)
server = uvicorn.Server(config)
server.run()
