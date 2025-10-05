import asyncio
import json
import logging
import time
import uuid

from fastapi import APIRouter, Request, Response
from screamrouter_audio_engine import AudioManager

logger = logging.getLogger(__name__)

class APIWebRTC:
    def __init__(self, app: APIRouter, audio_manager: AudioManager):
        self.app = app
        self.audio_manager = audio_manager
        self.router = APIRouter()
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
                    sink_id = info.get("sink_id")
                    if sink_id:
                        try:
                            self.audio_manager.remove_webrtc_listener(sink_id, listener_id)
                            logger.info(f"Successfully removed stale webrtc listener '{listener_id}' for sink '{sink_id}'.")
                        except Exception as e:
                            logger.warning(f"Failed to remove stale listener '{listener_id}': {e}")
                    
                    # Clean up other resources
                    if listener_id in self.ice_candidates:
                        del self.ice_candidates[listener_id]
                    if listener_id in self.pending_server_candidates:
                        del self.pending_server_candidates[listener_id]

    async def whep_heartbeat(self, sink_id: str, listener_id: str):
        """
        Handles the WHEP heartbeat to keep the session alive.
        """
        async with self.listeners_lock:
            if listener_id in self.listeners_info:
                self.listeners_info[listener_id]["last_heartbeat"] = time.time()
                logger.debug(f"Heartbeat successful for listener '{listener_id}'.")
                return Response(status_code=204)
            else:
                # If listener is not tracked, it might be stale or invalid.
                # Tell the client it's gone so it can clean up.
                logger.warning(f"Heartbeat received for untracked/stale listener '{listener_id}'.")
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
            success = self.audio_manager.add_webrtc_listener(
                sink_id,
                listener_id,
                offer_sdp,
                on_local_description,
                on_ice_candidate,
                client_ip
            )

            if not success:
                raise RuntimeError("Failed to add WebRTC listener in audio engine.")

            # Wait for the on_local_description callback to be called.
            await asyncio.wait_for(answer_sdp_event.wait(), timeout=5.0)

            if not answer_sdp:
                raise RuntimeError("Audio engine did not provide an SDP answer in time.")

            # If successful, add to our tracked listeners
            async with self.listeners_lock:
                self.listeners_info[listener_id] = {
                    "sink_id": sink_id,
                    "last_heartbeat": time.time(),
                    "ip": client_ip
                }
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
            
            self.audio_manager.add_webrtc_remote_ice_candidate(sink_id, listener_id, candidate, sdp_mid)
            logger.info(f"[whep:{listener_id}] Relayed ICE candidate to C++ engine.")
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

            success = self.audio_manager.remove_webrtc_listener(sink_id, listener_id)
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