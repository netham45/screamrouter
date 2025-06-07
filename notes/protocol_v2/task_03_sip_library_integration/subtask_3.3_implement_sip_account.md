# Sub-Task 3.3: Implement `SipAccount` Class for Event Handling

**Objective:** Create a Python class `SipAccount` that inherits from `pjsua2.Account`. This class will override PJSIP callback methods (`onRegState()`, `onIncomingCall()`, `onInstantMessage()`, etc.) to handle SIP events like device registration, incoming calls (for WebRTC signaling), and keep-alives.

**Parent Task:** [SIP Library (pjproject) Integration](../task_03_sip_library_integration.md)
**Previous Sub-Task:** [Sub-Task 3.2: Implement `SipManager` Class and PJSIP Initialization](./subtask_3.2_implement_sip_manager_pjsip_init.md)

## Key Steps & Considerations:

1.  **`SipAccount` Class Definition (`src/sip_server/sip_account.py` - new file):**
    *   This class will act as the primary handler for SIP interactions related to ScreamRouter's role as a presence server.
    *   It needs access to its parent `SipManager` (for PJSIP operations) and `ConfigurationManager` (to update device states).

    ```python
    # In src/sip_server/sip_account.py
    import pjsua2 as pj
    import logging
    # from screamrouter_logger import get_logger # Or your custom logger
    # logger = get_logger(__name__)
    logger = logging.getLogger(__name__)

    # Forward declaration for type hinting if SipManager is in a different file and imported
    # from typing import TYPE_CHECKING
    # if TYPE_CHECKING:
    #     from .sip_manager import SipManager 
    #     from ...configuration.configuration_manager import ConfigurationManager

    class SipAccount(pj.Account):
        def __init__(self, sip_manager, config_manager):
            super().__init__()
            self.sip_manager = sip_manager # Instance of SipManager
            self.config_manager = config_manager # Instance of ConfigurationManager
            self.active_calls = {} # To manage SipCall instances for INVITEs

        def onRegState(self, prm: pj.OnRegStateParam):
            """
            Callback when registration state changes.
            ScreamRouter acts as a registrar, so this is mainly for server-side processing of REGISTER requests.
            PJSIP's default Account might not fully expose incoming REGISTER details this way if it's not set up as a registrar.
            We might need to handle REGISTER requests more directly via onRxRequest if this callback isn't suitable for acting as a server.
            
            For now, this is a placeholder. True REGISTER handling might be in onRxRequest for non-INVITEs.
            The primary mechanism for devices "registering" with ScreamRouter will be by sending REGISTER messages.
            The server doesn't register itself anywhere.
            """
            logger.info(f"SipAccount onRegState: code={prm.code}, reason={prm.reason}, "
                        f"expiration={prm.expiration}")
            # This callback is more for when THIS account registers with a remote server.
            # For ScreamRouter acting as a server, we'll likely use onIncomingCall for INVITEs
            # and potentially onRxRequest for REGISTER messages if not handled by a specific registrar setup.

        def onIncomingCall(self, prm: pj.OnIncomingCallParam):
            """
            Handle incoming INVITE requests, primarily for WebRTC signaling.
            """
            call = SipCall(self, self.sip_manager, self.config_manager, call_id=prm.callId)
            self.active_calls[prm.callId] = call # Store the call

            call_op_prm = pj.CallOpParam()
            call_op_prm.statusCode = pj.PJSIP_SC_OK # Default to 200 OK for now, may change to 180 Ringing first
            
            # Log incoming call details
            rdata = self.sip_manager.ep.utilStrError(0) # Get remote info from callId
            # This is not the right way to get remote info. prm.rdata should have it.
            # Or call.getInfo()
            
            call_info = call.getInfo()
            logger.info(f"Incoming call from {call_info.remoteUri} to {call_info.localUri} [Call ID: {prm.callId}]")
            logger.debug(f"  Remote Contact: {call_info.remoteContact}")
            logger.debug(f"  Call SDP: \n{prm.rdata.wholeMsg}") # prm.rdata.wholeMsg contains the full SIP message with SDP

            # Answer the call. For WebRTC, this usually means accepting the offer.
            # The actual SDP answer will be constructed later after C++ processing.
            try:
                call.answer(call_op_prm) 
                # The SipCall object will handle SDP offer/answer exchange.
            except pj.Error as e:
                logger.error(f"Error answering incoming call: {e.info()}")
                del self.active_calls[prm.callId] # Clean up
                # Optionally send an error response if answer fails
                # call_op_prm.statusCode = pj.PJSIP_SC_INTERNAL_SERVER_ERROR
                # call.hangup(call_op_prm)

        # onInstantMessage might be used for OPTIONS keep-alives if they are sent as MESSAGE
        def onInstantMessage(self, prm: pj.OnInstantMessageParam):
            logger.info(f"Instant message from {prm.fromUri}: {prm.body}")
            # Potentially handle OPTIONS here if sent as MESSAGE, or use onRxRequest.

        # This is a more generic callback that can catch REGISTER, OPTIONS, etc.
        # It's powerful but requires more manual parsing of the SIP message.
        def onRxRequest(self, rdata: pj.SipRxData):
            """
            Generic callback for any incoming SIP request.
            This is where we'll likely handle REGISTER and OPTIONS.
            """
            method_name = rdata.msgC SipMethod().name # Get method name (REGISTER, OPTIONS, etc.)
            logger.info(f"SipAccount onRxRequest: Received {method_name} from {rdata.srcAddress}")
            logger.debug(f"Full request:\n{rdata.wholeMsg}")

            if method_name == "REGISTER":
                self.handle_register_request(rdata)
            elif method_name == "OPTIONS":
                self.handle_options_request(rdata)
            # elif method_name == "INVITE":
                # This is typically handled by onIncomingCall, but onRxRequest sees it too.
                # onIncomingCall provides a pj.Call object automatically.
                # If handling here, would need to create pj.Call manually or decide.
            #    pass
            else:
                # Send a 405 Method Not Allowed for unhandled methods by default
                self.send_stateless_response(rdata, pj.PJSIP_SC_METHOD_NOT_ALLOWED, "Method Not Allowed")

            return True # Indicate request was handled (or will be)

        def handle_register_request(self, rdata: pj.SipRxData):
            # Parse From, To, Contact, Expires headers
            # Parse SDP body if present for capabilities
            # Extract UUID (e.g., from From URI user part or custom header)
            # Call self.config_manager.handle_sip_registration(...)
            # Send 200 OK response
            logger.info(f"Handling REGISTER from {rdata.srcAddress}")
            # Placeholder for detailed parsing and response
            # Example: Send 200 OK (stateless for now, stateful would involve registrar logic)
            self.send_stateless_response(rdata, pj.PJSIP_SC_OK, "OK")


        def handle_options_request(self, rdata: pj.SipRxData):
            # Update device last-seen time in ConfigurationManager
            # Send 200 OK response with Allow/Accept headers
            logger.info(f"Handling OPTIONS from {rdata.srcAddress}")
            # Placeholder for detailed parsing and response
            self.send_stateless_response(rdata, pj.PJSIP_SC_OK, "OK")
            
        def send_stateless_response(self, rdata: pj.SipRxData, status_code: int, reason_phrase: str):
            try:
                resp_prm = pj.SendRequestOptions()
                # Create response using endpoint's createRequest method (seems counter-intuitive but is used for responses too)
                # Or more directly, construct a response message.
                # PJSIP has tdata_create_response for stateless responses.
                # This part needs careful implementation based on PJSIP examples for stateless replies.
                # For now, logging the intent.
                logger.info(f"Intending to send {status_code} {reason_phrase} for request from {rdata.srcAddress}")
                # Actual sending logic for stateless responses needs pj.Endpoint.utilCreateResponseMsg or similar
                # and then pj.Endpoint.sendResponse.
            except pj.Error as e:
                logger.error(f"Error sending stateless response: {e.info()}")


    class SipCall(pj.Call):
        """
        Manages a single SIP call, primarily for WebRTC signaling (INVITE sessions).
        """
        def __init__(self, account: SipAccount, sip_manager, config_manager, call_id=pj.PJSUA_INVALID_ID):
            super().__init__(account, call_id)
            self.account = account
            self.sip_manager = sip_manager
            self.config_manager = config_manager
            self.webrtc_sink_id = None # To be determined, e.g. from To URI or SDP
            logger.info(f"SipCall created [Call ID: {self.getId()}]")

        def onCallState(self, prm: pj.OnCallStateParam):
            ci = self.getInfo()
            logger.info(f"Call {self.getId()} state: {ci.stateText}")
            if ci.state == pj.PJSIP_INV_STATE_DISCONNECTED:
                logger.info(f"Call {self.getId()} disconnected: reason={ci.lastStatusCode} ({ci.lastReason})")
                # Clean up this call from SipAccount's active_calls
                if self.getId() in self.account.active_calls:
                    del self.account.active_calls[self.getId()]
                # Notify C++ layer if WebRTC session needs cleanup
                if self.webrtc_sink_id:
                    # self.config_manager.notify_webrtc_session_ended(self.webrtc_sink_id)
                    pass
        
        def onCallMediaState(self, prm: pj.OnCallMediaStateParam):
            # For WebRTC with data channels, media state might not be as relevant as data channel state.
            # However, if audio/video streams were used, this would be important.
            ci = self.getInfo()
            logger.info(f"Call {self.getId()} media state: {ci.mediaState}")

        def onCallSdpCreated(self, prm: pj.OnCallSdpCreatedParam):
            """
            Called when local SDP is created (e.g., an answer to an offer).
            This SDP needs to be sent to the C++ WebRTCSender.
            """
            logger.info(f"Call {self.getId()} local SDP created:\n{prm.sdp.wholeSdp}")
            # This SDP is usually the *answer* generated by PJSIP after C++ provides its media capabilities.
            # The flow is:
            # 1. Web client sends OFFER.
            # 2. SipCall.onIncomingCall -> SipCall gets created.
            # 3. SipCall extracts offer, sends to C++ WebRTCSender via AudioManager/ConfigManager.
            # 4. C++ WebRTCSender processes offer, generates its part of the ANSWER, sends back to Python.
            # 5. Python (SipCall) combines C++ part with PJSIP's network part and calls call.answer() with this complete SDP.
            # This onCallSdpCreated might be if PJSIP itself generates an offer (e.g. for an outgoing call).
            # For an incoming call where we construct the answer, we'd use prm.rdata.wholeMsg in onIncomingCall for the offer.

        # Other pj.Call callbacks as needed (e.g., onDtmf, onCallTransferStatus)
    ```

2.  **In `SipManager.start()`:**
    *   Create an instance of this `SipAccount`.
    *   Configure it with a local URI (e.g., `sip:screamrouter@<configured_domain>`).
    *   Call `self.default_account.create(acc_cfg)` to make it active.
    ```python
    # In SipManager.start() after libStart()
    # from .sip_account import SipAccount # Import the new class
    # self.default_account = SipAccount(self, self.config_manager) 
    # acc_cfg = pj.AccountConfig()
    # acc_cfg.idUri = f"sip:{self.sip_domain}" # e.g., "sip:screamrouter.local"
    # # Important: For a server/registrar, we don't register with anyone.
    # # Instead, this account is used to define callbacks for incoming requests.
    # # PJSIP might require at least one account to process incoming messages not tied to a dialog.
    # # Or, specific registrar setup might be needed.
    # # For now, creating a "null" registration account.
    # acc_cfg.regConfig.registrarUri = "" # No registration
    # try:
    //     self.default_account.create(acc_cfg)
    //     logger.info(f"Default SIP account created for URI {acc_cfg.idUri}")
    // except pj.Error as e:
    //     logger.error(f"Failed to create default SIP account: {e.info()}")
    //     raise # Propagate error
    ```
    *   **Note on Server-Side Account:** A PJSIP `Account` is typically for UACs (clients) registering to a UAS (server). For ScreamRouter acting as a server, the "default account" is more of a context for handling incoming requests that aren't part of an established dialog or for specific UAs. The `onRxRequest` callback on the `Endpoint` or a specially configured `Account` is key for server-like behavior for REGISTER.

3.  **Handling `REGISTER` (in `SipAccount.onRxRequest`):**
    *   Parse the `rdata` (SipRxData) for `REGISTER` messages.
    *   Extract `From`, `To`, `Contact`, `Expires` headers.
    *   Extract device UUID (e.g., from `From` URI user part, or a custom `X-UUID` header).
    *   Parse SDP body if present (using `sdp_utils.py` from `task_03`) for capabilities.
    *   Call `self.config_manager.handle_sip_registration(uuid, ip, port, role, sdp_caps)`.
    *   Send a `200 OK` response. This needs to be done carefully, possibly statelessly or by creating a server transaction.

4.  **Handling `INVITE` (in `SipAccount.onIncomingCall`):**
    *   A new `SipCall` object (subclass of `pj.Call`) is created.
    *   Store this `SipCall` instance (e.g., in `SipAccount.active_calls` keyed by call ID).
    *   Parse the SDP offer from `prm.rdata.wholeMsg`.
    *   Relay the SDP offer to `ConfigurationManager`, which then forwards to C++ `AudioManager` -> `WebRTCSender`.
    *   When C++ provides an SDP answer, `SipCall` will use `call.answer(call_op_prm_with_sdp)` to send it.

5.  **Handling `OPTIONS` (in `SipAccount.onRxRequest`):**
    *   Parse `OPTIONS` requests.
    *   Extract device identifier (e.g., from `From` URI).
    *   Update the device's last-seen time in `ConfigurationManager`.
    *   Respond with `200 OK`, possibly including `Allow` and `Accept` headers indicating server capabilities.

## Code Alterations:

*   **New File:** `src/sip_server/sip_account.py` - Implement `SipAccount` and `SipCall` classes.
*   **Modified File:** `src/sip_server/sip_manager.py`
    *   Import `SipAccount`.
    *   Instantiate and create `self.default_account` in `start()`.
    *   Clean up `self.default_account` in `stop()`.
*   **New File (from `task_03` plan):** `src/sip_server/sdp_utils.py` - Will be used by `SipAccount` to parse SDP.

## Recommendations:

*   **`onRxRequest` vs. Specific Callbacks:** `onRxRequest` is powerful for server-side handling of non-INVITE requests like REGISTER and OPTIONS. `onIncomingCall` is convenient for INVITEs as it provides a `pj.Call` object.
*   **Stateless vs. Stateful Responses:** For `REGISTER` and `OPTIONS`, PJSIP allows sending stateless responses, which is simpler if complex transaction management isn't needed.
*   **SDP Parsing:** The `sdp_utils.py` module will be crucial for extracting meaningful information from SDP bodies in REGISTER or INVITE messages.
*   **Error Handling in Callbacks:** Callbacks executed by PJSIP threads should not raise unhandled Python exceptions, as this can crash the PJSIP thread or the application. Wrap callback logic in `try...except` blocks.
*   **SIP Message Parsing:** PJSIP provides utilities to parse SIP headers and message bodies. Refer to `pjsua2` documentation for `SipHeader`, `SipMsgBody`, etc.

## Acceptance Criteria:

*   `SipAccount` class is defined and inherits from `pj.Account`.
*   `SipCall` class is defined and inherits from `pj.Call`.
*   `SipManager` creates and manages a `SipAccount` instance.
*   `onRxRequest` in `SipAccount` can receive and log basic details of incoming REGISTER and OPTIONS requests.
*   `onIncomingCall` in `SipAccount` can receive and log basic details of incoming INVITE requests, creating a `SipCall` object.
*   Placeholders for calling `ConfigurationManager` methods are present.
