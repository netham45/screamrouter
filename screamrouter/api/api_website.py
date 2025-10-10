"""Holds the API endpoints to serve files for html/javascript/css"""
import mimetypes
import os
from typing import List, Optional, Union

import httpx
from fastapi import FastAPI, HTTPException, Request, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse, RedirectResponse, StreamingResponse
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates
from starlette.templating import _TemplateResponse
from starlette.websockets import WebSocketState

from screamrouter.configuration.configuration_manager import ConfigurationManager
from screamrouter.constants import constants
from screamrouter.constants.constants import SITE_DIR
from screamrouter.screamrouter_logger.screamrouter_logger import get_logger
from screamrouter.screamrouter_types.annotations import (RouteNameType, SinkNameType,
                                                SourceNameType)
from screamrouter.screamrouter_types.configuration import (Equalizer, RouteDescription,
                                                  SinkDescription,
                                                  SourceDescription)
from screamrouter.screamrouter_types.website import (AddEditRouteInfo, AddEditSinkInfo,
                                            AddEditSourceInfo,
                                            EditEqualizerInfo)
from screamrouter.api.api_vnc_websocket import handle_vnc_websocket

logger = get_logger(__name__)


SITE_PREFIX: str = "/site"
TEMPLATE_DIRECTORY_PREFIX: str = "."

class APIWebsite():
    """Holds the API endpoints to serve files for html/javascript/css"""
    def __init__(self, main_api: FastAPI, screamrouter_configuration: ConfigurationManager):
        self.main_api = main_api
        """Main FastAPI instance"""
        self.screamrouter_configuration:ConfigurationManager = screamrouter_configuration
        """ScreamRouter Configuration Manager"""
        self.main_api.add_middleware(
            CORSMiddleware,
            allow_origins=["*"],
            allow_credentials=True,
            allow_methods=["*"],
            allow_headers=["*"],
        )
        self.main_api.get(f"{SITE_PREFIX}/vnc/{{source_name}}",
                          tags=["Site Resources"])(self.vnc)
        self.main_api.websocket("/ws/vnc/{source_name}")(self.websocket_vnc_proxy)
        # Mount noVNC from the repository's site/noVNC directory (not from within SITE_DIR)
        novnc_path = os.path.join(os.path.dirname(SITE_DIR), "site", "noVNC")
        self.main_api.mount("/site/noVNC", StaticFiles(directory=novnc_path), name="noVNC")
        self.main_api.mount("/site/static", StaticFiles(directory=os.path.join(SITE_DIR, "static")), name="static")
        self.main_api.get("/favicon.ico", tags=["Site Resources"])(self.favicon)

        self._templates = Jinja2Templates(directory=SITE_DIR)
        mimetypes.add_type('application/javascript', '.js')
        mimetypes.add_type('text/css', '.css')
        mimetypes.add_type('audio/mpeg', '.mp3')
        logger.info("[Website] Endpoints added")
        if constants.NPM_REACT_DEBUG_SITE:
            self.main_api.get("/site/{path:path}", name="site2")(self.proxy_npm_devsite)
        else:
            self.main_api.get("/site/{path:path}", name="site")(self.serve_static_or_index)
            self.main_api.get("/site", name="site")(self.serve_index)
        self.main_api.get("/", name="site")(self.redirect_index)

    async def proxy_npm_devsite(self, request: Request, path: str):
        async with httpx.AsyncClient(base_url="http://localhost:8080/") as client:
            # Construct the new URL
            if ".js" not in path and ".css" not in path and ".svg" not in path:
                url = f"/site/"
            else:
                url = f"/site/{path}"
            logger.debug(f"Path: {url}")
            
            # Forward the request headers
            headers = dict(request.headers)
            headers.pop("host", None)
            
            # Remove the 'Accept-Encoding' header to prevent compression
            headers.pop("accept-encoding", None)
            
            # Forward the request
            method = request.method
            content = await request.body()
            
            response = await client.request(
                method,
                url,
                headers=headers,
                content=content,
                params=request.query_params,
            )
        
            # Determine the correct content type
            content_type = response.headers.get("content-type")
            if not content_type:
                if path.endswith('.js'):
                    content_type = 'application/javascript'
                elif path.endswith('.css'):
                    content_type = 'text/css'
                else:
                    content_type = mimetypes.guess_type(path)[0] or 'application/octet-stream'

            # Update the response headers with the correct content type
            response_headers = dict(response.headers)
            response_headers['content-type'] = content_type

            # Remove any content-encoding header to ensure uncompressed response
            response_headers.pop('content-encoding', None)

            # Return the proxied response
            return StreamingResponse(
                response.iter_bytes(),
                status_code=response.status_code,
                headers=response_headers
            )


    async def serve_static_or_index(self, request: Request, path: str) -> FileResponse:
        """
        Serve static files or index.html based on the requested path. This is for React compatibility.

        Args:
            request (Request): The incoming request object.
            path (str): The requested path.

        Returns:
            Union[FileResponse, HTTPException]: The file response or a 404 error.
        """
        file_path: str = os.path.join(SITE_DIR, path)

        if os.path.isfile(file_path):
            # If the requested path is a file, serve it directly
            return FileResponse(file_path)
        elif os.path.isdir(file_path) and os.path.isfile(os.path.join(file_path, "index.html")):
            # If the path is a directory and contains an index.html, serve that
            return FileResponse(os.path.join(file_path, "index.html"))
        else:
            # If the file doesn't exist, try to serve index.html from the root
            index_path: str = os.path.join(SITE_DIR, "index.html")
            if os.path.isfile(index_path):
                return FileResponse(index_path)
            else:
                # If root index.html doesn't exist, raise a 404 error
                raise HTTPException(status_code=404, detail="File not found")

    async def serve_index(self) -> FileResponse:
        """
        Serve the main index.html file.

        Returns:
            FileResponse: The index.html file response.
        """
        return FileResponse(os.path.join(SITE_DIR, "index.html"))

    async def redirect_index(self) -> RedirectResponse:
        """
        Redirect to the /site/ URL.

        Returns:
            RedirectResponse: A redirect response to /site/.
        """
        return RedirectResponse(url="/site/")

    def favicon(self):
        """Favicon handler"""
        return FileResponse(os.path.join(SITE_DIR, "favicon.ico"))

    def return_template(self, request: Request,
                              template_name: str,
                              additional_context: Optional[dict] = None,
                              media_type: str = 'text/html') -> _TemplateResponse:
        """Returns a template"""
        sources: List[SourceDescription]
        sinks: List[SinkDescription]
        routes: List[RouteDescription]
        sources, sinks, routes = self.screamrouter_configuration.get_website_context()
        context: dict = {"sources": sources, "sinks": sinks, "routes": routes}
        if not additional_context is None:
            context.update(additional_context)
        return self._templates.TemplateResponse(
            request=request, name=template_name,
            context=context, media_type=media_type)

    # VNC endpoint
    def vnc(self, request: Request, source_name: SourceNameType):
        """Returns the VNC viewer dialog that uses the new WebSocket endpoint"""
        # Get the source configuration to validate it exists and has VNC enabled
        source: SourceDescription = self.screamrouter_configuration.get_source_by_name(source_name)
        
        if not source:
            raise HTTPException(status_code=404, detail=f"Source '{source_name}' not found")
        
        if not source.vnc_ip:
            raise HTTPException(status_code=400, detail=f"VNC is not enabled for source '{source_name}'")
        
        logger.info("[Website] Returning VNC viewer for source: %s", source_name)
        
        # Return the VNC viewer template with the source name
        # The template will construct the WebSocket URL using the source_name
        return self.return_template(request,
                "dialog_views/vnc.html.jinja",
                {"source_name": source_name,
                 "ws_path": f"/ws/vnc/{source_name}"})

    async def websocket_vnc_proxy(self, websocket: WebSocket, source_name: str):
        """
        WebSocket endpoint for VNC proxy connections.
        
        This endpoint handles VNC connections through native FastAPI WebSocket
        support, replacing the websockify subprocess-based approach.
        
        Args:
            websocket: FastAPI WebSocket connection
            source_name: Name of the source requesting VNC access
        """
        logger.info(f"VNC WebSocket connection requested for source: {source_name}")
        
        # Get the source configuration
        source = self.screamrouter_configuration.get_source_by_name(source_name)
        
        if not source:
            logger.error(f"Source '{source_name}' not found in configuration")
            await websocket.close(code=1008, reason=f"Source '{source_name}' not found")
            return
        
        # Check if VNC is enabled for this source
        if not source.vnc_ip:
            logger.error(f"VNC is not enabled for source '{source_name}'")
            await websocket.close(code=1008, reason=f"VNC is not enabled for source '{source_name}'")
            return
        
        # Get VNC configuration - ensure host is converted to string
        vnc_config = {
            "host": str(source.vnc_ip),
            "port": int(source.vnc_port)
        }
        
        logger.info(f"Starting VNC WebSocket proxy for source '{source_name}' to {vnc_config['host']}:{vnc_config['port']}")
        
        try:
            # Handle the WebSocket connection through our proxy
            await handle_vnc_websocket(websocket, source_name, vnc_config)
        except Exception as e:
            logger.error(f"Error in VNC WebSocket proxy for source '{source_name}': {e}")
            if websocket.client_state == WebSocketState.CONNECTED:
                await websocket.close(code=1011, reason="Internal server error")

    def stop(self):
        """Clean up method - currently no cleanup needed"""
        pass

    # Site resource endpoints
    def site_index(self, request: Request):
        """Index page"""
        return self.return_template(request, "index.html.jinja")

    def site_index_body(self, request: Request):
        """Index page"""
        return self.return_template(request, "index_body.html.jinja")


    def site_javascript(self, request: Request):
        """Javascript page"""
        return self.return_template(request, "screamrouter.js.jinja",
                                    None, "text/javascript")

    def site_css(self, request: Request):
        """CSS page"""
        return self.return_template(request, "screamrouter.css.jinja",
                                    None, "text/css")

    def add_sink(self, request: Request):
        """Return the HTML dialog to add a sink"""
        request_info: AddEditSinkInfo
        request_info = AddEditSinkInfo(add_new=True,
                                       data=SinkDescription(name="New Sink"))
        return self.return_template(request,
                                    "dialog_views/add_edit_sink.html.jinja",
                                    {"request_info": request_info})

    def add_sink_group(self, request: Request):
        """Return the HTML dialog to add a sink group"""
        request_info: AddEditSinkInfo
        request_info = AddEditSinkInfo(add_new=True,
                                       data=SinkDescription(name="New Sink"))
        return self.return_template(request,
                                "dialog_views/add_edit_group.html.jinja",
                                {"request_info": request_info,
                                "holder_type": "sink"})

    def edit_sink(self, request: Request, sink_name: SinkNameType):
        """Return the HTML dialog to edit a sink"""
        existing_sink: SinkDescription
        existing_sink = self.screamrouter_configuration.get_sink_by_name(sink_name)
        request_info: AddEditSinkInfo
        request_info = AddEditSinkInfo(add_new=False,
                                       data=existing_sink)
        if existing_sink.is_group:
            return self.return_template(request,
                                    "dialog_views/add_edit_group.html.jinja",
                                    {"request_info": request_info,
                                     "holder_name": existing_sink.name,
                                     "holder_type": "sink"})
        else:
            return self.return_template(request,
                                        "dialog_views/add_edit_sink.html.jinja",
                                        {"request_info": request_info})

    def edit_sink_equalizer(self, request: Request, sink_name: SinkNameType):
        """Return the HTML dialog to edit a sink"""
        existing_sink_equalizer: Equalizer
        existing_sink_equalizer = self.screamrouter_configuration.get_sink_by_name(
                                                                            sink_name).equalizer
        request_info: EditEqualizerInfo
        request_info = EditEqualizerInfo(add_new=False, data=existing_sink_equalizer,
                                         equalizer_holder_name=sink_name,
                                         equalizer_holder_type="sink")
        return self.return_template(request,
                                    "dialog_views/equalizer.html.jinja",
                                    {"request_info": request_info})

    def add_source(self, request: Request):
        """Return the HTML dialog to add a source"""
        request_info: AddEditSourceInfo
        request_info = AddEditSourceInfo(add_new=True,
                                       data=SourceDescription(name="New Source"))
        return self.return_template(request,
                                    "dialog_views/add_edit_source.html.jinja",
                                    {"request_info": request_info})

    def add_source_group(self, request: Request):
        """Return the HTML dialog to add a source group"""
        request_info: AddEditSourceInfo
        request_info = AddEditSourceInfo(add_new=True,
                                       data=SourceDescription(name="New Source"))
        return self.return_template(request,
                                "dialog_views/add_edit_group.html.jinja",
                                {"request_info": request_info,
                                "holder_type": "source"})

    def edit_source(self, request: Request, source_name: SourceNameType):
        """Return the HTML dialog to edit a source"""
        existing_source: SourceDescription
        existing_source = self.screamrouter_configuration.get_source_by_name(source_name)
        request_info: AddEditSourceInfo
        request_info = AddEditSourceInfo(add_new=False,
                                       data=existing_source)
        if existing_source.is_group:
            return self.return_template(request,
                                    "dialog_views/add_edit_group.html.jinja",
                                    {"request_info": request_info,
                                     "holder_name": existing_source.name,
                                     "holder_type": "source"})
        else:
            return self.return_template(request,
                                        "dialog_views/add_edit_source.html.jinja",
                                        {"request_info": request_info})

    def edit_source_equalizer(self, request: Request, source_name: SourceNameType):
        """Return the HTML dialog to edit a source"""
        existing_source_equalizer: Equalizer
        existing_source_equalizer = self.screamrouter_configuration.get_source_by_name(
                                                                            source_name).equalizer
        request_info: EditEqualizerInfo
        request_info = EditEqualizerInfo(add_new=False, data=existing_source_equalizer,
                                         equalizer_holder_name=source_name,
                                         equalizer_holder_type="source")
        return self.return_template(request,
                                    "dialog_views/equalizer.html.jinja",
                                    {"request_info": request_info})

    def add_route(self, request: Request):
        """Return the HTML dialog to add a route"""
        request_info: AddEditRouteInfo
        request_info = AddEditRouteInfo(add_new=True,
                                       data=RouteDescription(name="New Route"))
        return self.return_template(request,
                                    "dialog_views/add_edit_route.html.jinja",
                                    {"request_info": request_info})

    def edit_route(self, request: Request, route_name: RouteNameType):
        """Return the HTML dialog to edit a route"""
        existing_route: RouteDescription
        existing_route = self.screamrouter_configuration.get_route_by_name(route_name)
        request_info: AddEditRouteInfo
        request_info = AddEditRouteInfo(add_new=False,
                                       data=existing_route)
        return self.return_template(request,
                                    "dialog_views/add_edit_route.html.jinja",
                                    {"request_info": request_info})

    def edit_route_equalizer(self, request: Request, route_name: RouteNameType):
        """Return the HTML dialog to edit a route"""
        existing_route_equalizer: Equalizer
        existing_route_equalizer = self.screamrouter_configuration.get_route_by_name(
                                                                            route_name).equalizer
        request_info: EditEqualizerInfo
        request_info = EditEqualizerInfo(add_new=False, data=existing_route_equalizer,
                                         equalizer_holder_name=route_name,
                                         equalizer_holder_type="route")
        return self.return_template(request,
                                    "dialog_views/equalizer.html.jinja",
                                    {"request_info": request_info})

    def edit_sink_routes(self, request: Request, sink_name: SinkNameType):
        """Return a body to edit sink routes"""
        existing_sink: SinkDescription = self.screamrouter_configuration.get_sink_by_name(
                                                                                        sink_name)
        request_info: AddEditSinkInfo
        request_info = AddEditSinkInfo(add_new=False,
                                       data=existing_sink)

        return self.return_template(request,
                                    "edit_body.html.jinja",
                                    {"request_info": request_info})

    def edit_source_routes(self, request: Request, source_name: SourceNameType):
        """Return a body to edit source routes"""
        existing_source: SourceDescription = self.screamrouter_configuration.get_source_by_name(
                                                                                    source_name)
        request_info: AddEditSourceInfo
        request_info = AddEditSourceInfo(add_new=False,
                                           data=existing_source)

        return self.return_template(request,
                                    "edit_body.html.jinja",
                                    {"request_info": request_info})
