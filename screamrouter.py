#!/usr/bin/python3
import os
from typing import List
import api
from fastapi import FastAPI

from api_webstream import API_webstream
from controller import Controller

import signal
import sys
       
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
websocket: API_webstream = API_webstream(app)
controller: Controller = Controller(websocket)

def signal_handler(sig, frame):
    controller.stop()
    os.kill(os.getpid(), signal.SIGTERM)

signal.signal(signal.SIGINT, signal_handler)

api = api.API(app, controller)
api.join()