"""Holds the API endpoints to serve files for html/javascript/css"""
from fastapi import FastAPI, Request
from fastapi.templating import Jinja2Templates
from logger import get_logger

logger = get_logger(__name__)

class APIWebsite():
    """Holds the API endpoints to serve files for html/javascript/css"""
    def __init__(self, app: FastAPI):
        self._app   = app
        """FastAPI"""
        self._app.get("/", tags=["Site"])(self.read_index)
        self._app.get("/site.js", tags=["Site"])(self.read_javascript)
        self._app.get("/site.css", tags=["Site"])(self.read_css)
        self._templates = Jinja2Templates(directory="./site/")

    # Site resource endpoints
    def read_index(self, request: Request):
        """Index page"""
        return self._templates.TemplateResponse(
            request=request, name="index.html"
            )

    def read_javascript(self, request: Request):
        """Javascript page"""
        return self._templates.TemplateResponse(
            request=request, name="site.js"
            )

    def read_css(self, request: Request):
        """CSS page"""
        return self._templates.TemplateResponse(
            request=request, name="site.css"
            )
