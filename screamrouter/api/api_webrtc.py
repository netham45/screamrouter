import asyncio
import json
import logging
import time
import uuid

from fastapi import APIRouter, Request, Response
from screamrouter_audio_engine import AudioManager
from screamrouter.configuration.configuration_manager import ConfigurationManager
from screamrouter.screamrouter_types.configuration import SinkDescription, RouteDescription, SourceDescription

logger = logging.getLogger(__name__)

class APIWebRTC:
    def __init__(self, app: APIRouter, audio_manager: AudioManager, configuration_manager: ConfigurationManager = None):
        self.app = app
        self.audio_manager = audio_manager
        self.configuration_manager = configuration_manager
        self.router = APIRouter()
        
        # Setup endpoints for temporary entity creation
        self.router.add_api_route(
            "/api/listen/source/{source_id}/setup",
            self.setup_source_listener,
            methods=["POST"]
        )
        self.router.add_api_route(
            "/api/listen/route/{route_id}/setup",
            self.setup_route_listener,
            methods=["POST"]
        )
        self.router.add_api_route(
            "/api/listen/temporary/{sink_id}",
            self.cleanup_temporary_sink,
            methods=["DELETE"],
            status_code=204
        )
        
        # Existing sink endpoints
        self.router.add_api_route(
            "/api/whep/{sink_id}",
            self.whep_post,
            methods=["POST"],
            # Custom response handling to set headers and content type
            response_class=Response
        )
        self.router.add_api_route(
            "/api/whep/{sink_id}/{listener_id}",
            self.whep_patch,
            methods=["PATCH"],
            status_code=204
        )
        self.router.add_api_route(
            "/api/whep/{sink_id}/{listener_id}",
            self.whep_delete,
            methods=["DELETE"],
            status_code=204
        )
        self.router.add_api_route(
            "/api/whep/{sink_id}/{listener_id}/candidates",
            self.whep_get_candidates,
            methods=["GET"]
        )
        self.router.add_api_route(
            "/api/whep/{sink_id}/{listener_id}",
            self.whep_heartbeat,
            methods=["POST"],
            status_code=204
        )
        
        self.app.include_router(self.router)
        # A simple in-memory store for ICE candidates.
        # In a real production app, this should be a more robust solution
        # like a database or a distributed cache (e.g., Redis).
        self.ice_candidates = {}
        # Store for pending server ICE candidates that need to be sent to clients
        self.pending_server_candidates = {}
        # For tracking listener heartbeats and cleaning up stale sessions
        self.listeners_info = {}
        self.listeners_lock = asyncio.Lock()
        # Track temporary entities created for WebRTC listeners
        self.temporary_entities = {}
        self.temporary_entities_lock = asyncio.Lock()

    def start_background_tasks(self):
        """Starts the background task for checking stale listeners."""
        asyncio.create_task(self._check_stale_listeners())

    async def _check_stale_listeners(self):
        """Periodically checks for and removes stale WebRTC listeners."""
        while True:
            await asyncio.sleep(5)
            now = time.time()
            stale_ids = []
            
            async with self.listeners_lock:
                for listener_id, info in self.listeners_info.items():
                    if now - info.get("last_heartbeat", now) > 15:
                        stale_ids.append(listener_id)
            
            for listener_id in stale_ids:
                logger.info(f"Listener {listener_id} timed out due to no heartbeat. Cleaning up.")
                info = None
                async with self.listeners_lock:
                    # pop is atomic and will prevent whep_delete from trying to delete it again
                    info = self.listeners_info.pop(listener_id, None)
                
                if info:
                    entity_type = info.get("entity_type", "sink")
                    entity_id = info.get("entity_id", info.get("sink_id"))
                    
                    if entity_id:
                        try:
                            self.audio_manager.remove_webrtc_listener(entity_id, listener_id)
                            logger.info(f"Successfully removed stale webrtc listener '{listener_id}' for {entity_type} '{entity_id}'.")
                        except Exception as e:
                            logger.warning(f"Failed to remove stale listener '{listener_id}': {e}")
                    
                    # Clean up temporary entities if they were created for this listener
                    await self._cleanup_temporary_entities(listener_id)
                    
                    # Clean up other resources
                    if listener_id in self.ice_candidates:
                        del self.ice_candidates[listener_id]
                    if listener_id in self.pending_server_candidates:
                        del self.pending_server_candidates[listener_id]

    async def whep_heartbeat(self, sink_id: str, listener_id: str):
        """
        Handles the WHEP heartbeat to keep the session alive.
        """
        logger.debug(f"[whep_heartbeat] Received heartbeat - sink_id: '{sink_id}', listener_id: '{listener_id}'")
        logger.debug(f"[whep_heartbeat] Current tracked listeners: {list(self.listeners_info.keys())}")
        
        async with self.listeners_lock:
            if listener_id in self.listeners_info:
                self.listeners_info[listener_id]["last_heartbeat"] = time.time()
                logger.debug(f"[whep_heartbeat] Heartbeat successful for listener '{listener_id}' (sink: '{sink_id}').")
                return Response(status_code=204)
            else:
                # If listener is not tracked, it might be stale or invalid.
                # Tell the client it's gone so it can clean up.
                logger.warning(f"[whep_heartbeat] Heartbeat received for untracked/stale listener '{listener_id}' (sink: '{sink_id}'). Tracked listeners: {list(self.listeners_info.keys())}")
                return Response(status_code=404, content="WebRTC session not found.")

    async def whep_post(self, sink_id: str, request: Request):
        """
        Handles the whep POST request to initiate a WebRTC session.
        This endpoint receives an SDP offer and returns an SDP answer.
        """
        # whep requires a specific content type
        if request.headers.get("Content-Type") != "application/sdp":
            return Response(
                status_code=415,
                content="Unsupported Media Type: Content-Type must be application/sdp"
            )

        client_ip = request.client.host

        try:
            offer_sdp_bytes = await request.body()
            offer_sdp = offer_sdp_bytes.decode("utf-8")
        except Exception as e:
            logger.error(f"Could not read SDP offer from request body: {e}")
            return Response(status_code=400, content="Bad Request: Could not read SDP offer.")

        listener_id = str(uuid.uuid4())
        logger.info(f"Received whep request for sink '{sink_id}'. Assigning listener_id '{listener_id}'")
        
        # For regular sinks, check if we need to use config_id
        actual_sink_id = sink_id
        if self.configuration_manager:
            try:
                # Try to get the sink to see if we should use its config_id
                sink = self.configuration_manager.get_sink_by_name(sink_id)
                if sink and sink.config_id:
                    actual_sink_id = sink.config_id
                    logger.debug(f"[whep] Using config_id '{actual_sink_id}' for sink '{sink_id}'")
            except:
                # If sink not found by name, it might already be a config_id
                logger.debug(f"[whep] Using sink_id as-is: '{sink_id}'")

        # Prepare a list to store ICE candidates for this listener
        self.ice_candidates[listener_id] = []

        def on_ice_candidate(candidate: str, sdp_mid: str):
            """
            Callback function passed to the C++ audio engine.
            It's called from a C++ thread when a new ICE candidate is generated.
            """
            logger.info(f"[whep:{listener_id}] C++ produced ICE candidate: {candidate} with sdpMid: {sdp_mid}")
            # Store server ICE candidates for the client to poll
            candidate_data = {"candidate": candidate, "sdpMid": sdp_mid}
            if listener_id not in self.pending_server_candidates:
                self.pending_server_candidates[listener_id] = []
            self.pending_server_candidates[listener_id].append(candidate_data)
            logger.info(f"[whep:{listener_id}] Stored server ICE candidate for client polling")

        try:
            answer_sdp_event = asyncio.Event()
            answer_sdp = None

            def on_local_description(sdp_str: str):
                nonlocal answer_sdp
                logger.info(f"[whep:{listener_id}] C++ produced local description (answer).")
                answer_sdp = sdp_str
                answer_sdp_event.set()

            # This call is now non-blocking and returns immediately.
            logger.info(f"[whep] Adding WebRTC listener to sink '{actual_sink_id}' (original: '{sink_id}')")
            success = self.audio_manager.add_webrtc_listener(
                actual_sink_id,  # Use the actual sink_id (which might be config_id)
                listener_id,
                offer_sdp,
                on_local_description,
                on_ice_candidate,
                client_ip
            )

            if not success:
                logger.error(f"[whep] Failed to add WebRTC listener for sink '{actual_sink_id}'")
                raise RuntimeError("Failed to add WebRTC listener in audio engine.")

            # Wait for the on_local_description callback to be called.
            await asyncio.wait_for(answer_sdp_event.wait(), timeout=5.0)

            if not answer_sdp:
                raise RuntimeError("Audio engine did not provide an SDP answer in time.")

            # If successful, add to our tracked listeners
            async with self.listeners_lock:
                self.listeners_info[listener_id] = {
                    "sink_id": actual_sink_id,  # Store the actual sink_id used
                    "original_sink_id": sink_id,  # Keep original for reference
                    "last_heartbeat": time.time(),
                    "ip": client_ip
                }
                logger.info(f"[whep_post] Stored listener '{listener_id}' for sink '{sink_id}' with actual_sink_id '{actual_sink_id}'")
        except Exception as e:
            logger.error(f"Failed to create whep listener for sink '{sink_id}': {e}", exc_info=True)
            # Clean up if the listener was partially created
            if listener_id in self.ice_candidates:
                del self.ice_candidates[listener_id]
            
            # Also remove from our tracking
            async with self.listeners_lock:
                self.listeners_info.pop(listener_id, None)
            return Response(status_code=500, content="Failed to create WebRTC session in audio engine.")

        # Construct the 201 Created response as per the whep specification
        location_url = f"/api/whep/{sink_id}/{listener_id}"
        headers = {
            "Location": location_url,
            "Content-Type": "application/sdp"
        }
        
        logger.info(f"Successfully created whep listener '{listener_id}'. Returning SDP answer.")
        return Response(content=answer_sdp, status_code=201, headers=headers)

    async def whep_patch(self, sink_id: str, listener_id: str, request: Request):
        """
        Handles the whep PATCH request to trickle ICE candidates.
        """
        # whep requires a specific content type for trickling ICE
        if "application/json" not in request.headers.get("Content-Type", ""):
            return Response(
                status_code=415,
                content="Unsupported Media Type: Content-Type must be application/json"
            )

        try:
            candidate_json_bytes = await request.body()
            candidate_data = json.loads(candidate_json_bytes)
            candidate = candidate_data.get("candidate")
            sdp_mid = candidate_data.get("sdpMid")
            print(candidate_json_bytes)
            print(candidate_data)
            if not candidate or not sdp_mid:
                return Response(status_code=400, content="Bad Request: 'candidate' and 'sdpMid' fields are required.")
            
            # Get the actual sink_id from listener info
            actual_sink_id = sink_id
            async with self.listeners_lock:
                if listener_id in self.listeners_info:
                    actual_sink_id = self.listeners_info[listener_id].get("sink_id", sink_id)
            
            self.audio_manager.add_webrtc_remote_ice_candidate(actual_sink_id, listener_id, candidate, sdp_mid)
            logger.info(f"[whep:{listener_id}] Relayed ICE candidate to C++ engine for sink '{actual_sink_id}'.")
            return Response(status_code=204)
        except Exception as e:
            logger.error(f"Failed to process ICE candidate for listener '{listener_id}': {e}", exc_info=True)
            return Response(status_code=500, content="Failed to process ICE candidate.")

    async def whep_delete(self, sink_id: str, listener_id: str):
        """
        Handles the whep DELETE request to terminate a session.
        """
        try:
            # Remove from our tracking first to prevent race conditions with the cleanup task
            info = None
            async with self.listeners_lock:
                info = self.listeners_info.pop(listener_id, None)

            # Only proceed if we actually had this listener tracked
            if not info:
                logger.debug(f"Listener '{listener_id}' already removed or never existed.")
                return Response(status_code=204)  # Consider it successful

            # Get the actual sink_id from the info
            actual_sink_id = info.get("sink_id", sink_id)
            success = self.audio_manager.remove_webrtc_listener(actual_sink_id, listener_id)
            # Clean up stored ICE candidates
            if listener_id in self.ice_candidates:
                del self.ice_candidates[listener_id]
            if listener_id in self.pending_server_candidates:
                del self.pending_server_candidates[listener_id]
            
            if success:
                logger.info(f"Successfully removed whep listener '{listener_id}' for sink '{sink_id}'.")
                return Response(status_code=204)
            else:
                logger.warning(f"Attempted to remove non-existent whep listener '{listener_id}'.")
                return Response(status_code=404, content="WebRTC session not found.")
        except Exception as e:
            logger.error(f"Failed to remove whep listener '{listener_id}': {e}", exc_info=True)
            return Response(status_code=500, content="Failed to remove WebRTC session.")

    async def whep_get_candidates(self, sink_id: str, listener_id: str):
        """
        Handles GET requests for server ICE candidates.
        Returns any pending server ICE candidates for the client.
        """
        try:
            if listener_id not in self.pending_server_candidates:
                return Response(content="[]", status_code=200, headers={"Content-Type": "application/json"})
            
            # Get all pending candidates and clear the list
            candidates = self.pending_server_candidates[listener_id]
            self.pending_server_candidates[listener_id] = []
            
            logger.debug(f"[whep:{listener_id}] Returning {len(candidates)} server ICE candidates to client")
            return Response(content=json.dumps(candidates), status_code=200, headers={"Content-Type": "application/json"})
        except Exception as e:
            logger.error(f"Failed to get ICE candidates for listener '{listener_id}': {e}", exc_info=True)
            return Response(status_code=500, content="Failed to get ICE candidates.")
    
    async def _cleanup_temporary_entities(self, listener_id: str):
        """Clean up any temporary entities created for a listener"""
        async with self.temporary_entities_lock:
            if listener_id in self.temporary_entities:
                entities = self.temporary_entities.pop(listener_id)
                if self.configuration_manager:
                    for entity_type, entity_name in entities:
                        try:
                            if entity_type == "sink":
                                self.configuration_manager.remove_temporary_sink(entity_name)
                                logger.info(f"Removed temporary sink '{entity_name}' for listener '{listener_id}'")
                            elif entity_type == "route":
                                self.configuration_manager.remove_temporary_route(entity_name)
                                logger.info(f"Removed temporary route '{entity_name}' for listener '{listener_id}'")
                        except Exception as e:
                            logger.warning(f"Failed to remove temporary {entity_type} '{entity_name}': {e}")
    
    
    async def setup_source_listener(self, source_id: str, request: Request):
        """
        Creates temporary sink and route for listening to a source.
        Returns the temporary sink ID that can be used with /api/whep/{sink_id}.
        
        This endpoint separates the entity creation from WebRTC connection setup,
        allowing the frontend to:
        1. Create temporary entities
        2. Use existing WHEP endpoints with the temporary sink
        3. Clean up when done
        """
        if not self.configuration_manager:
            return Response(status_code=500, content=json.dumps({"error": "Configuration manager not available"}))
        
        try:
            # Verify source exists
            source = self.configuration_manager.get_source_by_name(source_id)
            if not source:
                return Response(
                    status_code=404,
                    content=json.dumps({"error": f"Source '{source_id}' not found"})
                )
            
            # Generate unique IDs for temporary entities
            temp_id = str(uuid.uuid4())[:8]
            temp_sink_name = f"temp_source_{source_id}_{temp_id}"
            temp_route_name = f"temp_route_{source_id}_{temp_id}"
            
            # Create temporary sink
            temp_sink = SinkDescription(
                name=temp_sink_name,
                ip="127.0.0.1",  # Local WebRTC sink
                port=1234,  # Port will be assigned by WebRTC
                protocol="web_receiver",
                enabled=True,
                channels=2,
                sample_rate=48000,
                bit_depth=16,
                is_temporary=True
            )
            
            # Add temporary sink
            logger.info(f"[setup_source_listener] Creating temporary sink '{temp_sink_name}' for source '{source_id}'")
            if not self.configuration_manager.add_temporary_sink(temp_sink):
                raise RuntimeError(f"Failed to create temporary sink for source '{source_id}'")
            
            # Get the sink back to retrieve its config_id
            created_sink = self.configuration_manager.get_sink_by_name(temp_sink_name)
            if not created_sink:
                # Clean up if we can't get the sink back
                self.configuration_manager.remove_temporary_sink(temp_sink_name)
                raise RuntimeError(f"Failed to retrieve temporary sink for source '{source_id}'")
            
            sink_config_id = created_sink.config_id
            
            # Create temporary route from source to temporary sink
            temp_route = RouteDescription(
                name=temp_route_name,
                source=source_id,
                sink=temp_sink_name,
                enabled=True
            )
            
            # Add temporary route
            logger.info(f"[setup_source_listener] Creating temporary route '{temp_route_name}' from source '{source_id}' to sink '{temp_sink_name}'")
            if not self.configuration_manager.add_temporary_route(temp_route):
                # Clean up the temporary sink if route creation fails
                self.configuration_manager.remove_temporary_sink(temp_sink_name)
                raise RuntimeError(f"Failed to create temporary route for source '{source_id}'")
            
            # Return the temporary sink information
            response_data = {
                "sink_id": sink_config_id,  # Use config_id for WHEP endpoints
                "sink_name": temp_sink_name,
                "route_name": temp_route_name,
                "source_id": source_id
            }
            
            logger.info(f"[setup_source_listener] Successfully created temporary entities for source '{source_id}': sink_id='{sink_config_id}'")
            return Response(
                status_code=200,
                content=json.dumps(response_data),
                headers={"Content-Type": "application/json"}
            )
            
        except Exception as e:
            logger.error(f"Failed to setup source listener for '{source_id}': {e}", exc_info=True)
            return Response(
                status_code=500,
                content=json.dumps({"error": f"Failed to setup source listener: {str(e)}"})
            )
    
    async def setup_route_listener(self, route_id: str, request: Request):
        """
        Creates temporary sink and route for listening to a route.
        Returns the temporary sink ID that can be used with /api/whep/{sink_id}.
        
        This endpoint separates the entity creation from WebRTC connection setup,
        allowing the frontend to:
        1. Create temporary entities
        2. Use existing WHEP endpoints with the temporary sink
        3. Clean up when done
        """
        if not self.configuration_manager:
            return Response(status_code=500, content=json.dumps({"error": "Configuration manager not available"}))
        
        try:
            # Verify route exists
            route = self.configuration_manager.get_route_by_name(route_id)
            if not route:
                return Response(
                    status_code=404,
                    content=json.dumps({"error": f"Route '{route_id}' not found"})
                )
            
            # Get the original sink's properties to copy them
            original_sink_name = route.sink
            original_sink = self.configuration_manager.get_sink_by_name(original_sink_name)
            if not original_sink:
                return Response(
                    status_code=500,
                    content=json.dumps({"error": f"Original sink '{original_sink_name}' not found for route '{route_id}'"})
                )
            
            # Generate unique IDs for temporary entities
            temp_id = str(uuid.uuid4())[:8]
            temp_sink_name = f"temp_route_{route_id}_{temp_id}"
            temp_route_name = f"temp_mirror_{route_id}_{temp_id}"
            
            # Create temporary sink with same properties as original
            temp_sink = SinkDescription(
                name=temp_sink_name,
                ip="127.0.0.1",  # Local WebRTC sink
                port=1234,  # Port will be assigned by WebRTC
                protocol="web_receiver",
                enabled=True,
                channels=original_sink.channels,
                sample_rate=original_sink.sample_rate,
                bit_depth=original_sink.bit_depth,
                channel_layout=original_sink.channel_layout,
                is_temporary=True
            )
            
            # Add temporary sink
            logger.info(f"[setup_route_listener] Creating temporary sink '{temp_sink_name}' for route '{route_id}'")
            if not self.configuration_manager.add_temporary_sink(temp_sink):
                raise RuntimeError(f"Failed to create temporary sink for route '{route_id}'")
            
            # Get the sink back to retrieve its config_id
            created_sink = self.configuration_manager.get_sink_by_name(temp_sink_name)
            if not created_sink:
                # Clean up if we can't get the sink back
                self.configuration_manager.remove_temporary_sink(temp_sink_name)
                raise RuntimeError(f"Failed to retrieve temporary sink for route '{route_id}'")
            
            sink_config_id = created_sink.config_id
            
            # Create a new temporary route that mirrors the original but outputs to WebRTC
            temp_route = RouteDescription(
                name=temp_route_name,
                source=route.source,
                sink=temp_sink_name,
                enabled=True,
                volume=route.volume,
                equalizer=route.equalizer,
                delay=route.delay,
                timeshift=route.timeshift
            )
            
            # Add temporary route
            logger.info(f"[setup_route_listener] Creating temporary route '{temp_route_name}' mirroring route '{route_id}'")
            if not self.configuration_manager.add_temporary_route(temp_route):
                # Clean up the temporary sink if route creation fails
                self.configuration_manager.remove_temporary_sink(temp_sink_name)
                raise RuntimeError(f"Failed to create temporary route for route '{route_id}'")
            
            # Return the temporary sink information
            response_data = {
                "sink_id": sink_config_id,  # Use config_id for WHEP endpoints
                "sink_name": temp_sink_name,
                "route_name": temp_route_name,
                "original_route_id": route_id
            }
            
            logger.info(f"[setup_route_listener] Successfully created temporary entities for route '{route_id}': sink_id='{sink_config_id}'")
            return Response(
                status_code=200,
                content=json.dumps(response_data),
                headers={"Content-Type": "application/json"}
            )
            
        except Exception as e:
            logger.error(f"Failed to setup route listener for '{route_id}': {e}", exc_info=True)
            return Response(
                status_code=500,
                content=json.dumps({"error": f"Failed to setup route listener: {str(e)}"})
            )
    
    async def cleanup_temporary_sink(self, sink_id: str):
        """
        Removes temporary entities (sink and associated routes) when done.
        
        This should be called when the WebRTC connection is closed or
        when the user is done listening to clean up resources.
        
        Args:
            sink_id: The config_id or name of the temporary sink to remove
        """
        if not self.configuration_manager:
            return Response(status_code=500, content=json.dumps({"error": "Configuration manager not available"}))
        
        try:
            # Try to find the sink by config_id first, then by name
            sink = None
            try:
                # First try as config_id
                sinks = self.configuration_manager.get_sinks()
                for s in sinks:
                    if s.config_id == sink_id:
                        sink = s
                        break
            except:
                pass
            
            # If not found by config_id, try by name
            if not sink:
                sink = self.configuration_manager.get_sink_by_name(sink_id)
            
            if not sink:
                logger.warning(f"[cleanup_temporary_sink] Sink '{sink_id}' not found")
                # Return 204 anyway as it might have been already cleaned up
                return Response(status_code=204)
            
            sink_name = sink.name
            
            # Find and remove routes that use this sink
            routes = self.configuration_manager.get_routes()
            removed_routes = []
            for route in routes:
                if route.sink == sink_name and hasattr(route, 'is_temporary') and route.is_temporary:
                    if self.configuration_manager.remove_temporary_route(route.name):
                        removed_routes.append(route.name)
                        logger.info(f"[cleanup_temporary_sink] Removed temporary route '{route.name}'")
            
            # Remove the temporary sink
            if self.configuration_manager.remove_temporary_sink(sink_name):
                logger.info(f"[cleanup_temporary_sink] Removed temporary sink '{sink_name}' (id: '{sink_id}')")
            
            logger.info(f"[cleanup_temporary_sink] Cleanup complete for sink '{sink_id}': removed {len(removed_routes)} routes")
            return Response(status_code=204)
            
        except Exception as e:
            logger.error(f"Failed to cleanup temporary sink '{sink_id}': {e}", exc_info=True)
            return Response(
                status_code=500,
                content=json.dumps({"error": f"Failed to cleanup temporary sink: {str(e)}"})
            )