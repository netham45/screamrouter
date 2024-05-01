#!/usr/bin/python3
"""ScreamRouter"""
import os
import threading
import signal

from fastapi import FastAPI

from configuration.configuration_controller import ConfigurationController

from api.api_configuration import APIConfiguration
from api.api_webstream import APIWebStream
from api.api_website import APIWebsite
from logger import get_logger

logger = get_logger(__name__)

def signal_handler(sig, frame):
    """Fired when Ctrl+C pressed"""
    logger.error("Ctrl+C pressed %s %s", sig, frame)
    controller.stop()
    # Wouldn't it be cool if uvicorn provided a real way to exit?
    os.kill(os.getpid(), signal.SIGTERM)

signal.signal(signal.SIGINT, signal_handler)

threading.current_thread().name = "ScreamRouter Main Thread"

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

controller: ConfigurationController = ConfigurationController(None)
api_controller = APIConfiguration(app, controller)
webstream: APIWebStream = APIWebStream(app)
website: APIWebsite = APIWebsite(app)
controller.set_webstream(webstream)
api_controller.join()
