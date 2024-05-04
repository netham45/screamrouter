#!/usr/bin/python3
"""ScreamRouter"""
import os
import threading
import signal
import uvicorn

from fastapi import FastAPI

from configuration.configuration_manager import ConfigurationManager

from api.api_configuration import APIConfiguration
from api.api_webstream import APIWebStream
from api.api_website import APIWebsite
from logger import get_logger
import constants


os.nice(-15)

logger = get_logger(__name__)

threading.current_thread().name = "ScreamRouter Main Thread"

def signal_handler(sig, frame):
    """Fired when Ctrl+C pressed"""
    logger.error("Ctrl+C pressed %s %s", sig, frame)
    try:
        controller.stop()
    except NameError:
        pass
    # Wouldn't it be cool if uvicorn provided a real way to exit?
    os.kill(os.getpid(), signal.SIGTERM)

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
webstream: APIWebStream = APIWebStream(app)
controller: ConfigurationManager = ConfigurationManager(webstream)
api_controller = APIConfiguration(app, controller)
website: APIWebsite = APIWebsite(app)
uvicorn.run(app,
            port=constants.API_PORT,
            host=constants.API_HOST,
            log_config="uvicorn_log_config.yaml" if constants.LOG_TO_FILE else None)
