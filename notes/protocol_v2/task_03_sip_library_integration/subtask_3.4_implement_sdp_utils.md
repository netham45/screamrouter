# Sub-Task 3.4: Implement SDP Parsing Utilities (`sdp_utils.py`)

**Objective:** Create helper functions in `src/sip_server/sdp_utils.py` to parse relevant information from SDP bodies received in SIP messages (e.g., from REGISTER or INVITE). This includes extracting media descriptions, RTP map attributes, custom attributes, and WebRTC-related information.

**Parent Task:** [SIP Library (pjproject) Integration](../task_03_sip_library_integration.md)
**Previous Sub-Task:** [Sub-Task 3.3: Implement `SipAccount` Class for Event Handling](./subtask_3.3_implement_sip_account.md)

## Key Steps & Considerations:

1.  **Create `src/sip_server/sdp_utils.py`:**
    *   This new file will contain functions for SDP parsing.
    *   It will likely use regular expressions (`re` module) for parsing, as full SDP parser libraries might be overkill for targeted extraction.

2.  **Function to Parse SDP String into Lines/Sections:**
    *   A basic utility to split the SDP string by lines and potentially group `m=` sections.
    ```python
    # In src/sip_server/sdp_utils.py
    import re
    import logging
    logger = logging.getLogger(__name__)

    def parse_sdp(sdp_str: str) -> dict:
        """
        Parses an SDP string into a more structured dictionary.
        Focuses on extracting key information relevant to ScreamRouter.
        """
        parsed_sdp = {
            "session_attributes": [],
            "media_sections": []
        }
        current_media_section = None

        if not sdp_str:
            return parsed_sdp

        for line in sdp_str.splitlines():
            line = line.strip()
            if not line:
                continue

            try:
                type_char, value = line.split("=", 1)
            except ValueError:
                logger.warning(f"Malformed SDP line: {line}")
                continue

            if type_char == "m":
                if current_media_section:
                    parsed_sdp["media_sections"].append(current_media_section)
                current_media_section = {"attributes": [], "raw_m_line": value}
                # Parse m-line: m=<media> <port> <proto> <fmt> ...
                parts = value.split()
                if len(parts) >= 4:
                    current_media_section["media_type"] = parts[0]
                    current_media_section["port"] = int(parts[1])
                    current_media_section["protocol"] = parts[2]
                    current_media_section["formats"] = parts[3:]
                else:
                     logger.warning(f"Malformed m-line: {line}")
            elif type_char == "a":
                if current_media_section:
                    current_media_section["attributes"].append(value)
                else: # Session-level attribute
                    parsed_sdp["session_attributes"].append(value)
            # Add other SDP type_char handling (o, s, c, t, etc.) if needed
            # For now, focusing on m and a lines.
        
        if current_media_section: # Append the last media section
            parsed_sdp["media_sections"].append(current_media_section)
            
        return parsed_sdp
    ```

3.  **Functions to Extract Specific Attributes:**
    *   **`get_rtpmap_attributes(attributes: list) -> dict`:**
        *   Parses `a=rtpmap:<payload_type> <encoding_name>/<clock_rate>[/<encoding_params>]` lines.
        *   Returns a dictionary mapping payload type (int) to a dict of `{"encoding_name": str, "clock_rate": int, "encoding_params": str}`.
    *   **`get_fmtp_attributes(attributes: list) -> dict`:**
        *   Parses `a=fmtp:<payload_type> <format_specific_parameters>` lines.
        *   Returns a dictionary mapping payload type (int) to its format parameters string.
    *   **`get_custom_attribute(attributes: list, attr_name: str) -> Optional[str]`:**
        *   Parses generic `a=<attr_name>:<value>` or `a=<attr_name>` lines.
        *   Example usage: `get_custom_attribute(media_section["attributes"], "x-screamrouter-role")`.
    *   **`get_media_direction(attributes: list) -> Optional[str]`:**
        *   Parses `a=sendrecv`, `a=sendonly`, `a=recvonly`, `a=inactive`.

    ```python
    # In src/sip_server/sdp_utils.py (continued)

    def extract_rtpmap(attributes: list) -> dict:
        rtpmaps = {}
        for attr in attributes:
            match = re.match(r"rtpmap:(\d+)\s+([\w-]+)/(\d+)(?:/(\S+))?", attr)
            if match:
                pt, enc_name, clock_rate, enc_params = match.groups()
                rtpmaps[int(pt)] = {
                    "encoding_name": enc_name,
                    "clock_rate": int(clock_rate),
                    "encoding_params": enc_params if enc_params else None
                }
        return rtpmaps

    def extract_fmtp(attributes: list) -> dict:
        fmtps = {}
        for attr in attributes:
            match = re.match(r"fmtp:(\d+)\s+(.+)", attr)
            if match:
                pt, params = match.groups()
                fmtps[int(pt)] = params.strip()
        return fmtps

    def extract_attribute_value(attributes: list, key_prefix: str) -> Optional[str]:
        for attr in attributes:
            if attr.startswith(key_prefix):
                return attr[len(key_prefix):].strip()
        return None
    
    def extract_flag_attribute(attributes: list, key: str) -> bool:
        return key in attributes

    def get_device_capabilities_from_sdp(sdp_str: str) -> dict:
        """
        High-level function to extract key device capabilities.
        """
        parsed_sdp = parse_sdp(sdp_str)
        capabilities = {
            "uuid": None,
            "role": None, # "source" or "sink"
            "protocols": set(), # e.g., {"rtp", "webrtc"}
            "audio_formats": [], # List of dicts like {"codec": "L16", "rate": 48000, "channels": 2, "payload_type": 96}
            "rtp_port": None, # From m-line
            "ice_ufrag": None, # For WebRTC
            "ice_pwd": None,   # For WebRTC
            "dtls_fingerprint": None # For WebRTC
        }

        # Extract session-level attributes first
        capabilities["uuid"] = extract_attribute_value(parsed_sdp["session_attributes"], "x-screamrouter-uuid:")
        capabilities["role"] = extract_attribute_value(parsed_sdp["session_attributes"], "x-screamrouter-role:")
        
        supported_protocols_str = extract_attribute_value(parsed_sdp["session_attributes"], "x-screamrouter-supported-protocols:")
        if supported_protocols_str:
            capabilities["protocols"].update(p.strip() for p in supported_protocols_str.split(','))


        for media_section in parsed_sdp["media_sections"]:
            if media_section.get("media_type") == "audio":
                if "rtp" in media_section.get("protocol", "").lower() or "avp" in media_section.get("protocol", "").lower():
                    capabilities["protocols"].add("rtp")
                if media_section.get("port"):
                    capabilities["rtp_port"] = media_section["port"]

                rtpmaps = extract_rtpmap(media_section["attributes"])
                # fmtps = extract_fmtp(media_section["attributes"]) # If needed

                for pt, rtpmap_info in rtpmaps.items():
                    audio_format = {
                        "payload_type": pt,
                        "codec": rtpmap_info["encoding_name"],
                        "rate": rtpmap_info["clock_rate"],
                        "channels": None # Default, try to get from encoding_params (e.g., L16/48000/2)
                    }
                    if rtpmap_info["encoding_params"]:
                        try:
                            audio_format["channels"] = int(rtpmap_info["encoding_params"])
                        except ValueError:
                            logger.warning(f"Could not parse channels from rtpmap encoding_params: {rtpmap_info['encoding_params']}")
                    
                    # Simple channel inference for common PCM types
                    if audio_format["codec"].upper() == "L16" and not audio_format["channels"]:
                         audio_format["channels"] = 2 if not rtpmap_info["encoding_params"] else int(rtpmap_info["encoding_params"])


                    capabilities["audio_formats"].append(audio_format)
            
            # WebRTC specific (often m=application for data channels)
            if media_section.get("media_type") == "application" and "dtls/sctp" in media_section.get("protocol", "").lower():
                 capabilities["protocols"].add("webrtc")
                 capabilities["ice_ufrag"] = extract_attribute_value(media_section["attributes"], "ice-ufrag:")
                 capabilities["ice_pwd"] = extract_attribute_value(media_section["attributes"], "ice-pwd:")
                 capabilities["dtls_fingerprint"] = extract_attribute_value(media_section["attributes"], "fingerprint:") # Assuming one fingerprint line

            # Extract role and UUID from media attributes if not at session level
            if not capabilities["uuid"]:
                capabilities["uuid"] = extract_attribute_value(media_section["attributes"], "x-screamrouter-uuid:")
            if not capabilities["role"]:
                 capabilities["role"] = extract_attribute_value(media_section["attributes"], "x-screamrouter-role:")


        return capabilities
    ```

4.  **WebRTC Specific Parsing:**
    *   Functions to extract ICE candidates (`a=candidate:...`) and DTLS fingerprints (`a=fingerprint:...`). These are complex lines and might require more specific regex.
    *   `libdatachannel` itself can parse these from full SDP, so Python might only need to pass the raw SDP offer/answer to C++, and C++ passes its generated SDP/candidates back as strings. This sub-task focuses on what Python *needs* to interpret from an incoming client SDP for registration or basic understanding.

## Code Alterations:

*   **New File:** `src/sip_server/sdp_utils.py`
    *   Implement the parsing functions as sketched above.
*   **Modified File:** `src/sip_server/sip_account.py`
    *   Import and use functions from `sdp_utils.py` within `handle_register_request` and `onIncomingCall` to parse SDP bodies.
    ```python
    # In SipAccount.handle_register_request or onIncomingCall
    # from . import sdp_utils 
    # ...
    # if rdata.body: # Check if there's an SDP body
    #    sdp_body_str = rdata.body.data 
    #    capabilities = sdp_utils.get_device_capabilities_from_sdp(sdp_body_str)
    #    logger.info(f"Parsed SDP capabilities: {capabilities}")
    #    # Use capabilities to update ConfigurationManager
    # else:
    #    logger.info("No SDP body in REGISTER/INVITE.")
    ```

## Recommendations:

*   **Regex Precision:** SDP parsing with regex can be fragile. Focus on extracting the most critical information first and make regexes as robust as feasible. Consider well-known SDP parsing libraries if complexity grows significantly, but for targeted extraction, regex is often sufficient.
*   **Error Handling:** Gracefully handle malformed SDP lines or missing attributes.
*   **Extensibility:** Design functions to be somewhat extensible if more SDP attributes need to be parsed later.
*   **Testing:** Test with various example SDPs, including those from the `protocol_spec_v2.md`.

## Acceptance Criteria:

*   `sdp_utils.py` contains functions for parsing basic SDP structure and key attributes.
*   Can extract `m=` line details (media type, port, protocol, formats).
*   Can extract `a=rtpmap` attributes.
*   Can extract custom `a=x-screamrouter-uuid` and `a=x-screamrouter-role` attributes.
*   Can extract basic WebRTC signaling attributes if present (ICE ufrag/pwd, DTLS fingerprint).
*   `SipAccount` methods can utilize these utilities to get structured data from SDP strings.
