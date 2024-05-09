"""Holds the API endpoints to serve files for html/javascript/css"""
import mimetypes
from typing import List, Optional

from fastapi import FastAPI, Request
from fastapi.templating import Jinja2Templates
from starlette.templating import _TemplateResponse

from src.configuration.configuration_manager import ConfigurationManager
from src.screamrouter_logger.screamrouter_logger import get_logger
from src.screamrouter_types.annotations import (RouteNameType, SinkNameType,
                                                SourceNameType)
from src.screamrouter_types.configuration import (Equalizer, RouteDescription,
                                                  SinkDescription,
                                                  SourceDescription)
from src.screamrouter_types.website import (AddEditRouteInfo, AddEditSinkInfo,
                                            AddEditSourceInfo,
                                            EditEqualizerInfo)

logger = get_logger(__name__)

SITE_PREFIX="/site"
TEMPLATE_DIRECTORY_PREFIX="."

class APIWebsite():
    """Holds the API endpoints to serve files for html/javascript/css"""
    def __init__(self, main_api: FastAPI, screamrouter_configuration: ConfigurationManager):
        self.main_api = main_api
        """Main FastAPI instance"""
        self.screamrouter_configuration:ConfigurationManager = screamrouter_configuration
        """ScreamRouter Configuration Manager"""
        self.main_api.get("/", tags=["Site"])(self.site_index)
        self.main_api.get("/body", tags=["Site"])(self.site_index_body)
        self.main_api.get(f"{SITE_PREFIX}/screamrouter.js",
                          tags=["Site Resources"],)(self.site_javascript)
        self.main_api.get(f"{SITE_PREFIX}/screamrouter.css",
                          tags=["Site Resources"])(self.site_css)
        self.main_api.get(f"{SITE_PREFIX}/add_sink",
                          tags=["Site Resources"])(self.add_sink)
        self.main_api.get(f"{SITE_PREFIX}/edit_sink/{{sink_name}}",
                          tags=["Site Resources"])(self.edit_sink)
        self.main_api.get(f"{SITE_PREFIX}/edit_sink/{{sink_name}}/equalizer",
                          tags=["Site Resources"])(self.edit_sink_equalizer)
        self.main_api.get(f"{SITE_PREFIX}/add_source",
                          tags=["Site Resources"])(self.add_source)
        self.main_api.get(f"{SITE_PREFIX}/edit_source/{{source_name}}",
                          tags=["Site Resources"])(self.edit_source)
        self.main_api.get(f"{SITE_PREFIX}/edit_source/{{source_name}}/equalizer",
                          tags=["Site Resources"])(self.edit_source_equalizer)
        self.main_api.get(f"{SITE_PREFIX}/add_route",
                          tags=["Site Resources"])(self.add_route)
        self.main_api.get(f"{SITE_PREFIX}/edit_route/{{route_name}}",
                          tags=["Site Resources"])(self.edit_route)
        self.main_api.get(f"{SITE_PREFIX}/edit_route/{{route_name}}/equalizer",
                          tags=["Site Resources"])(self.edit_route_equalizer)
        self._templates = Jinja2Templates(directory="./site/")
        mimetypes.add_type('application/javascript', '.js')
        mimetypes.add_type('text/css', '.css')
        logger.info("[Website] Endpoints added")


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

    def edit_sink(self, request: Request, sink_name: SinkNameType):
        """Return the HTML dialog to edit a sink"""
        existing_sink: SinkDescription
        existing_sink = self.screamrouter_configuration.get_sink_by_name(sink_name)
        request_info: AddEditSinkInfo
        request_info = AddEditSinkInfo(add_new=False,
                                       data=existing_sink)
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

    def edit_source(self, request: Request, source_name: SourceNameType):
        """Return the HTML dialog to edit a source"""
        existing_source: SourceDescription
        existing_source = self.screamrouter_configuration.get_source_by_name(source_name)
        request_info: AddEditSourceInfo
        request_info = AddEditSourceInfo(add_new=False,
                                       data=existing_source)
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
