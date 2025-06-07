# Sub-Task 3.5: Integrate `SipManager` with `ConfigurationManager` and Zeroconf

**Objective:** Connect `SipManager` with `ConfigurationManager` for device state updates and WebRTC signaling relay. Also, ensure `SipManager` instructs the Zeroconf/mDNS publisher (from `task_05`) with its listening port and other necessary details.

**Parent Task:** [SIP Library (pjproject) Integration](../task_03_sip_library_integration.md)
**Previous Sub-Task:** [Sub-Task 3.4: Implement SDP Parsing Utilities (`sdp_utils.py`)](./subtask_3.4_implement_sdp_utils.md)
**Related Task:** [mDNS SRV Record Publishing for SIP Server](../task_05_zeroconf_publishing.md)

## Key Steps & Considerations:

### 1. `ConfigurationManager` Integration:
   *   **`SipManager` holds `ConfigurationManager` instance:**
      *   The `SipManager` constructor already takes `config_manager` (from Sub-Task 3.2).
   *   **`SipAccount` calls `ConfigurationManager`:**
      *   In `SipAccount.handle_register_request()`:
         *   After parsing REGISTER details (UUID, IP, port, role, SDP capabilities), call a method on `self.config_manager` like `handle_sip_registration(uuid, client_ip, client_port, sip_contact_uri, client_role, sdp_capabilities)`. This method is defined in `task_04_python_config_updates.md`.
      *   In `SipAccount.handle_options_request()` (or other keep-alive mechanism):
         *   Call a method like `self.config_manager.update_sip_device_last_seen(uuid)`.
      *   If a device times out (logic to be added in `SipManager` or `SipAccount`), call `self.config_manager.handle_sip_device_offline(uuid)`.
   *   **WebRTC Signaling Bridge (Python side in `ConfigurationManager`):**
      *   `ConfigurationManager` will expose methods callable by `SipCall` (or `SipAccount`) to forward WebRTC signaling messages to the C++ `AudioManager`.
         ```python
         # In src/configuration/configuration_manager.py
         # def forward_webrtc_sdp_to_cpp(self, sink_id: str, sdp_type: str, sdp_str: str):
         #     if self.audio_engine_client: # Instance of pybind11 audio_engine_python module
         #         logger.info(f"Forwarding WebRTC SDP {sdp_type} for sink {sink_id} to C++")
         #         self.audio_engine_client.handle_incoming_webrtc_signaling_message(sink_id, sdp_type, sdp_str)
         #     else:
         #         logger.error("Audio engine client not available to forward WebRTC SDP.")

         # def forward_webrtc_candidate_to_cpp(self, sink_id: str, candidate_str: str, mid: str, sdp_m_line_index: int):
         #     # Construct candidate JSON or use libdatachannel format if C++ expects it directly
         #     # For now, assume candidate_str is the direct string from libdatachannel/browser
         #     if self.audio_engine_client:
         #         logger.info(f"Forwarding WebRTC ICE candidate for sink {sink_id} to C++")
         #         self.audio_engine_client.handle_incoming_webrtc_signaling_message(sink_id, "candidate", candidate_str)
         #     else:
         #         logger.error("Audio engine client not available to forward WebRTC candidate.")
         ```
      *   `ConfigurationManager` needs to register a Python callback with C++ `AudioManager` (via `audio_engine_client.set_python_webrtc_signaling_callback`) to receive SDP/ICE from C++. This callback will then find the appropriate `SipCall` in `SipManager` and use it to send the message to the WebRTC client.
         ```python
         # In src/configuration/configuration_manager.py
         # def _init_audio_engine_callbacks(self):
         #     if self.audio_engine_client:
         #         self.audio_engine_client.set_python_webrtc_signaling_callback(self._handle_cpp_webrtc_signaling)
         
         # def _handle_cpp_webrtc_signaling(self, sink_id: str, type: str, message: str):
         #     logger.info(f"Python received WebRTC signaling from C++ for sink {sink_id}: type={type}")
         #     if self.sip_manager: # SipManager instance
         #         self.sip_manager.forward_signaling_to_client(sink_id, type, message)
         #     else:
         #         logger.error("SipManager not available to forward C++ signaling.")

         # In src/sip_server/sip_manager.py
         # def forward_signaling_to_client(self, sink_id: str, type: str, message: str):
         #     # Find the SipCall associated with sink_id (e.g., stored during INVITE)
         #     # call_instance = self.find_call_for_sink(sink_id)
         #     # if call_instance:
         #     #    if type == "answer":
         #     #        call_instance.send_answer_sdp(message)
         #     #    elif type == "candidate":
         #     #        call_instance.send_ice_candidate(message)
         #     pass # Placeholder
         ```

### 2. Zeroconf/mDNS Integration:
   *   **`SipMdnsPublisher` (from `task_05_zeroconf_publishing.md`):**
      *   `SipManager` will instantiate and manage `SipMdnsPublisher`.
   *   **In `SipManager.start()`:**
      *   After the SIP transport is successfully created and listening:
         *   Get the router's UUID (e.g., from `self.config_manager.get_router_uuid()`).
         *   Get a configurable service name part (e.g., from global app settings or `ConfigurationManager`).
         *   Get the system hostname.
         *   Call `self.mdns_publisher.start_publishing(service_name, self.sip_port, router_uuid, hostname)`.
      ```python
      # In src/sip_server/sip_manager.py
      # from .mdns_publisher import SipMdnsPublisher # Assuming mdns_publisher.py is in the same directory
      # import socket

      # class SipManager:
      #     def __init__(self, config_manager, sip_port=5060, ...):
      #         # ...
      #         self.mdns_publisher = SipMdnsPublisher() # Or pass shared Zeroconf instance

      #     def start(self):
      #         # ... after self.ep.libStart() and transport creation ...
      #         try:
      #             router_uuid = self.config_manager.get_router_uuid() # Method to be added to ConfigManager
      #             service_name_part = self.config_manager.get_zeroconf_service_name() # e.g., "ScreamRouterLivingRoom"
      #             hostname = socket.gethostname() # Basic hostname
                        
      #             self.mdns_publisher.start_publishing(
      #                 service_name=service_name_part,
      #                 sip_port=self.sip_port,
      #                 router_uuid=router_uuid,
      #                 server_hostname=hostname 
      #             )
      #             logger.info("SIP service mDNS publishing started.")
      #         except Exception as e:
      #             logger.error(f"Failed to start mDNS publishing for SIP service: {e}")
      #         # ...
      
      #     def stop(self):
      #         # ...
      #         if self.mdns_publisher:
      #             self.mdns_publisher.stop_publishing()
      #             logger.info("SIP service mDNS publishing stopped.")
      #         # ...
      ```
   *   **In `SipManager.stop()`:**
      *   Call `self.mdns_publisher.stop_publishing()`.

## Code Alterations:

*   **`src/configuration/configuration_manager.py`:**
    *   Implement `handle_sip_registration(self, uuid: str, client_ip: str, client_port: int, sip_contact_uri: str, client_role: str, sdp_capabilities: dict)`.
    *   Implement `update_sip_device_last_seen(self, uuid: str)`.
    *   Implement `handle_sip_device_offline(self, uuid: str)`.
    *   Implement `forward_webrtc_sdp_to_cpp` and `forward_webrtc_candidate_to_cpp`.
    *   Implement `_init_audio_engine_callbacks` and `_handle_cpp_webrtc_signaling`.
    *   Add methods like `get_router_uuid()` and `get_zeroconf_service_name()`.
    *   Instantiate `SipManager` in its `__init__` or a start method, passing `self`.
    *   Ensure `SipManager.start()` and `stop()` are called during `ConfigurationManager`'s lifecycle.
*   **`src/sip_server/sip_manager.py`:**
    *   Import and instantiate `SipMdnsPublisher`.
    *   Call `mdns_publisher.start_publishing()` in `start()`.
    *   Call `mdns_publisher.stop_publishing()` in `stop()`.
    *   Implement `forward_signaling_to_client()` to interact with `SipCall` instances.
*   **`src/sip_server/sip_account.py`:**
    *   In `handle_register_request`, `onIncomingCall`, `handle_options_request`, call the respective methods on `self.config_manager` and `self.sip_manager` (for WebRTC signaling relay).
*   **`src/utils/mdns_publisher.py` (from `task_05`):**
    *   Ensure this module is created and `SipMdnsPublisher` class is defined as per `task_05_zeroconf_publishing.md`.

## Recommendations:

*   **Clear Separation of Concerns:** `SipManager` handles SIP protocol logic. `ConfigurationManager` handles state persistence and acts as a bridge to the C++ audio engine. `SipMdnsPublisher` handles mDNS.
*   **Error Handling:** Ensure robust error handling for calls between these components.
*   **Thread Safety:** Pay attention to thread safety if `ConfigurationManager` methods are called from PJSIP's threads (via `SipAccount` callbacks). Operations modifying shared state in `ConfigurationManager` might need locks if accessed from multiple threads (e.g., PJSIP thread and FastAPI/main thread).
*   **Configuration for Zeroconf:** The `service_name` part for mDNS should be user-configurable. The `router_uuid` should be persistent.

## Acceptance Criteria:

*   `SipAccount` correctly calls `ConfigurationManager` methods upon receiving REGISTER, OPTIONS, and INVITE messages.
*   `ConfigurationManager` can relay WebRTC signaling messages (SDP, ICE) between `SipManager` (Python) and `AudioManager` (C++).
*   `SipManager` successfully initializes and controls `SipMdnsPublisher` to advertise the SIP service via Zeroconf.
*   The overall system starts and stops cleanly with these integrations.
