#!/usr/bin/python3
import os
import threading
import signal

from fastapi import FastAPI

from configuration.configuration_controller import ConfigurationController

from api.api_configuration import API_Configuration
from api.api_webstream import API_Webstream
from api.api_website import API_Website

threading.current_thread().name = "ScreamRouter Main Thread"
       
tags_metadata = [
        {
            "name": "Sinks",
            "description": "API endpoints for managing Sinks"
        },
        {
            "name": "Sources",
            "description": "API endpoints for managing Sources"
        },
        {
            "name": "Routes",
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
    ]
app: FastAPI = FastAPI( title="ScreamRouter",
        description = "Routes PCM audio around for Scream sinks and sources",
#            summary = "Routes PCM audio around for Scream sinks and sources",
        version="0.0.1",
        contact={
            "name": "ScreamRouter",
            "url": "http://github.com/netham45/screamrouter",
        },
        license_info={
            "name": "No license chosen yet, all rights reserved",
        },
        openapi_tags=tags_metadata
    )
webstream: API_Webstream = API_Webstream(app)
website: API_Website = API_Website(app)
controller: ConfigurationController = ConfigurationController(webstream)

def signal_handler(sig, frame):
    controller.stop()
    os.kill(os.getpid(), signal.SIGTERM)

signal.signal(signal.SIGINT, signal_handler)

api_controller = API_Configuration(app, controller)
api_controller.join()