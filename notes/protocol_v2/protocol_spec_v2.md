# Scream Audio Protocol v2 Specification

## 1. Overview

Scream Audio Protocol v2 (SAPv2) is an evolution of the original Scream protocol, designed to enhance flexibility, interoperability, and device management. It introduces standardized protocols like RTP, SIP, and Zeroconf, while maintaining backward compatibility with existing Scream clients.

**Key Goals:**

*   **Modernized Transport:** Utilize RTP for robust audio streaming with proper timestamping and WebRTC for browser-based clients.
*   **Standardized Session Management:** Employ SIP for device registration, presence, and session negotiation.
*   **Simplified Discovery:** Use Zeroconf (DNS-SD) for easy discovery of the ScreamRouter instance.
*   **Enhanced Configuration:** Provide more granular control over audio parameters.
*   **Backward Compatibility:** Ensure existing Scream clients continue to function.

**Core Libraries:**

*   **RTP (C++):** oRTP
*   **WebRTC (C++):** libdatachannel
*   **MP3 (C++):** LibLAME (existing)
*   **SIP (Python):** pjproject (via Python bindings)

## 2. Device Discovery (Zeroconf)

Devices discover the ScreamRouter SIP Presence Server using Zeroconf (DNS-SD).

*   **Service Type:** `_screamrouter-sip._udp.local`
*   **Service Name:** User-configurable, e.g., "ScreamRouter Living Room"
*   **Port:** The port on which the ScreamRouter SIP Presence Server is listening.
*   **TXT Records:**
    *   `version`: Protocol version (e.g., "2.0")
    *   `router_uuid`: A unique identifier for the ScreamRouter instance.
    *   (Other records as needed)

Clients will query for this service type to find the IP address and port of the SIP server.

## 3. Session Negotiation & Management (SIP/SDP)

ScreamRouter acts as a SIP Presence Server. Devices register with the server to announce their presence and capabilities.

### 3.1. Device Registration (SIP REGISTER)

1.  **Client Initiates REGISTER:**
    *   The client (Source or Sink device) sends a SIP `REGISTER` request to the discovered SIP server.
    *   **Headers:**
        *   `To`: `sip:screamrouter@<server_domain_or_ip>`
        *   `From`: `sip:<device_uuid>@<client_ip_or_domain>` (Device UUID is crucial)
        *   `Contact`: `<sip:<device_uuid>@<client_ip>:<client_sip_port>;transport=udp>` (or tcp if supported)
        *   `Expires`: Registration expiry time (e.g., 3600 seconds). Clients must re-REGISTER before expiry.
        *   `User-Agent`: Client identifier string.
    *   **Body (SDP):** The `REGISTER` request SHOULD include an SDP body describing the client's capabilities:
        *   **Media Descriptions (`m=` lines):**
            *   For audio sources: `m=audio <port> RTP/AVP <payload_type_1> <payload_type_2> ...`
                *   `<port>`: Port the client will send RTP from (or 0 if dynamic).
                *   `<payload_type>`: Dynamic payload types for supported formats (e.g., L16/48000/2, MP3).
            *   For audio sinks: `m=audio <port> RTP/AVP <payload_type_1> <payload_type_2> ...`
                *   `<port>`: Port the client will receive RTP on.
        *   **Attributes (`a=` lines):**
            *   `a=rtpmap:<payload_type> L16/48000/2` (Example for 16-bit, 48kHz, Stereo PCM)
            *   `a=rtpmap:<payload_type> MP3/0/0` (MP3, sample rate/channels often implicit or in other SDP fields)
            *   `a=fmtp:<payload_type> ...` (Format-specific parameters)
            *   `a=sendrecv`, `a=sendonly`, `a=recvonly`: Indicates client role.
            *   `a=x-screamrouter-role:<role>`: Custom attribute, `<role>` can be "source" or "sink".
            *   `a=x-screamrouter-uuid:<device_uuid>`: Reinforce UUID.
            *   `a=x-screamrouter-supported-protocols:rtp,webrtc` (Comma-separated list)
            *   For WebRTC clients, SDP will include ICE candidates and DTLS fingerprints as per WebRTC standards.

2.  **Server Processes REGISTER:**
    *   ScreamRouter SIP server receives the `REGISTER`.
    *   It extracts the `device_uuid` from the `From` or `Contact` URI, or `x-screamrouter-uuid` attribute.
    *   It parses the SDP to understand client capabilities (role, supported formats, protocols).
    *   **ConfigurationManager Interaction:**
        *   If `device_uuid` is known: Update the existing `SourceDescription` or `SinkDescription`. Mark as online. Update IP, port, and capabilities based on the new REGISTER/SDP.
        *   If `device_uuid` is new: Create a new `SourceDescription` or `SinkDescription`.
            *   `name`: Derived from UUID or hostname.
            *   `uuid`: The `device_uuid`.
            *   `ip`: Client's IP from SIP message.
            *   `port`: Client's SIP port (or RTP port from SDP if applicable).
            *   `protocol_type`: "sip" (or "rtp", "webrtc" based on SDP).
            *   `role`: "source" or "sink" based on `x-screamrouter-role` or SDP media attributes.
            *   Populate format capabilities (channels, sample_rate, etc.) from SDP.
        *   The `ConfigurationManager` persists these changes to `config.yaml`.
    *   Server responds with `200 OK`.

### 3.2. Keep-Alive

*   Clients MUST re-REGISTER periodically before their registration expires (e.g., every hour, as indicated by `Expires` header).
*   Additionally, clients SHOULD send a SIP `OPTIONS` request (or a short-interval re-REGISTER) every 5-15 seconds to maintain their "active" status for real-time data flow.
*   If ScreamRouter doesn't receive a keep-alive within a configured timeout (e.g., 30 seconds), it marks the device as "offline" in `ConfigurationManager`, and audio transmission to/from that device may be paused or stopped.

### 3.3. Session Setup (for WebRTC or specific RTP parameter negotiation)

*   While basic RTP parameters can be derived from `REGISTER` SDP, more complex session setup (especially for WebRTC) will use SIP `INVITE`.
*   **Typical Flow (Client Initiated for WebRTC Sink):**
    1.  Client (Web Browser Sink) sends `INVITE` to ScreamRouter SIP server, including its SDP offer (ICE candidates, DTLS fingerprint, desired audio formats).
    2.  ScreamRouter (acting as a B2BUA or endpoint) processes the `INVITE`. If routing to a source, it might generate a corresponding `INVITE` to that source or use pre-configured source details.
    3.  ScreamRouter responds with `200 OK` containing its SDP answer (its ICE candidates, DTLS fingerprint, chosen audio format).
    4.  ICE negotiation and DTLS handshake proceed.
    5.  WebRTC Data Channel is established for MP3 streaming.
*   Similar flows can be used if RTP parameters need explicit negotiation beyond `REGISTER` SDP.

### 3.4. Legacy "scream" Protocol Devices

*   Devices configured with `protocol_type: "scream"` in `config.yaml` will bypass SIP registration and discovery.
*   ScreamRouter will communicate with them using the legacy direct IP/port mechanism.

## 4. Audio Transport

### 4.1. RTP (Real-time Transport Protocol)

*   **Usage:** Primary protocol for streaming PCM and MP3 audio between ScreamRouter and capable clients.
*   **Packetization:**
    *   **PCM Audio:**
        *   Payload Type: Dynamically assigned, indicated in SDP.
        *   Format: Typically L16 (16-bit linear PCM), but other bit depths (8, 24, 32) and sample rates (8kHz to 192kHz) can be negotiated via SDP.
        *   Channel Count: Negotiated via SDP.
        *   Payload: Raw PCM samples. The standard Scream 1152-byte chunk can be sent as the payload of one or more RTP packets. If chunking, ensure proper framing or use a payload format that supports fragmentation and reassembly (e.g., RFC 3551 for generic PCM).
        *   The existing 5-byte Scream header (sample rate, bit depth, channels, layout) MAY be prepended to the RTP payload if a custom RTP payload format is defined for Scream data. Otherwise, this information is conveyed via SDP and RTP header fields.
    *   **MP3 Audio:**
        *   Payload Type: Dynamically assigned (e.g., 14 for MP3 if standard, or dynamic).
        *   Format: MPEG Audio Layer III.
        *   Payload: One or more MP3 frames. RFC 2250 (RTP Payload Format for MPEG1/MPEG2 Video) can be adapted for audio, or a simpler scheme of sending MP3 frames directly.
*   **RTP Header Fields:**
    *   `Version (V)`: 2
    *   `Padding (P)`: 0 (typically)
    *   `Extension (X)`: 0 (typically, unless RTP header extensions are used)
    *   `CSRC count (CC)`: 0 (typically)
    *   `Marker (M)`: Set to 1 for the first packet of a talkspurt or a significant event (e.g., start of an MP3 file).
    *   `Payload Type (PT)`: As negotiated via SDP.
    *   `Sequence Number`: Incremented by one for each RTP data packet sent.
    *   `Timestamp`: Reflects the sampling instant of the first octet in the RTP data packet. Clock rate is typically the audio sample rate (e.g., 48000 Hz for 48kHz audio). Must be synchronized with the audio stream.
    *   `SSRC`: A randomly chosen 32-bit number unique for this RTP stream.
    *   `CSRC list`: Empty if CC is 0.
*   **Endianness:** All multi-byte fields in the RTP header are transmitted in network byte order (big-endian). PCM data within the payload should also adhere to network byte order if not implicitly defined by the payload format (e.g., L16 is often big-endian).

### 4.2. WebRTC (Web Real-Time Communication)

*   **Usage:** For streaming MP3 audio to/from web browser clients.
*   **Transport:** Secure Real-time Transport Protocol (SRTP) for media, DTLS for keying.
*   **Data Channels:** MP3 frames will be sent over WebRTC Data Channels (SCTP over DTLS over UDP).
    *   Reliable and ordered delivery is typical for data channels, suitable for MP3 frames.
*   **Signaling:** SIP (with SDP) will be used for WebRTC signaling (exchanging session descriptions, ICE candidates).

### 4.3. Legacy Scream Protocol

*   **Usage:** For backward compatibility with existing Scream clients.
*   **Transport:** UDP.
*   **Packet Format:**
    *   5-byte Scream Header:
        *   Byte 0: `(is_44100_base << 7) | samplerate_divisor`
        *   Byte 1: `bit_depth` (8, 16, 24, 32)
        *   Byte 2: `channels`
        *   Byte 3: `chlayout1` (Scream channel layout byte 1)
        *   Byte 4: `chlayout2` (Scream channel layout byte 2)
    *   Payload: 1152 bytes of raw PCM audio data.

## 5. Audio Format Handling

*   **Internal Processing:** ScreamRouter's C++ audio engine typically processes audio internally as 32-bit PCM at a high sample rate (e.g., 48kHz or configurable).
*   **Input Conversion:**
    *   RTP PCM Input: If the incoming RTP stream's PCM format (sample rate, bit depth, channels) differs from the internal processing format, the `AudioProcessor` class will be used for conversion.
    *   Legacy Scream Input: Converted as per existing mechanisms.
*   **Output Conversion:**
    *   RTP PCM Output: `AudioProcessor` converts internal audio to the negotiated RTP PCM format.
    *   RTP MP3 Output: Internal audio is converted to PCM suitable for LAME, then encoded to MP3 by LAME.
    *   WebRTC MP3 Output: Same as RTP MP3 Output.
    *   Legacy Scream Output: Converted as per existing mechanisms.
*   **Supported Parameters:**
    *   Sample Rates: 8kHz, 16kHz, 22.05kHz, 32kHz, 44.1kHz, 48kHz, (and higher, up to 192kHz if supported by `AudioProcessor` and libraries).
    *   Bit Depths: 8-bit, 16-bit, 24-bit, 32-bit PCM.
    *   Channels: Mono, Stereo, and multi-channel layouts (e.g., 5.1, 7.1) as supported by `AudioProcessor` and negotiated.

## 6. Configuration Data Model Changes

The following changes will be made to `SourceDescription` and `SinkDescription` in `src/screamrouter_types/configuration.py`:

*   `uuid: Optional[str] = None`
    *   Description: Universally Unique Identifier for the device. Used for SIP registration and persistent identification.
*   `protocol_type: Literal["scream", "rtp", "webrtc", "sip"] = "scream"`
    *   Description: Specifies the primary communication protocol for this device.
        *   `scream`: Legacy Scream protocol.
        *   `rtp`: Device communicates primarily via RTP (negotiated via SIP or manually configured).
        *   `webrtc`: Device is a WebRTC client.
        *   `sip`: Device is registered via SIP, actual media protocol (RTP/WebRTC) determined by SDP.
*   `sip_contact_uri: Optional[str] = None`
    *   Description: The SIP Contact URI of the registered device.
*   `rtp_payload_type_pcm: Optional[int] = None` (Example for RTP specific config)
    *   Description: Manually configured RTP payload type for PCM, if not using dynamic assignment via SDP.
*   `rtp_payload_type_mp3: Optional[int] = None` (Example for RTP specific config)
    *   Description: Manually configured RTP payload type for MP3.

Further nested Pydantic models (e.g., `RTPConfig`, `WebRTCConfig`) might be added if more detailed per-protocol configuration is required.

## 7. Security Considerations

*   **SIP:**
    *   Consider TLS for SIP signaling transport (`sips:` URI, `transport=tls`).
    *   Implement basic SIP authentication (Digest authentication) for `REGISTER` requests.
*   **RTP:**
    *   Use SRTP (Secure RTP) for encrypted media streams. Key exchange can be managed via SDES (in SDP) or DTLS-SRTP.
*   **WebRTC:**
    *   Inherently secure (DTLS for keying, SRTP for media, SCTP over DTLS for data channels).
*   **Zeroconf:**
    *   Operates on the local network. Standard mDNS security considerations apply.

## Appendix A: Example SDP

**Example SDP for an RTP Source (e.g., a microphone client):**

```sdp
v=0
o=<username> <sess-id> <sess-version> IN IP4 <client-ip>
s=ScreamRouter RTP Source
c=IN IP4 <client-ip>
t=0 0
m=audio <client-rtp-port> RTP/AVP 96 97
a=rtpmap:96 L16/48000/2
a=rtpmap:97 MP3/0/0
a=sendonly
a=x-screamrouter-role:source
a=x-screamrouter-uuid:xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
```

**Example SDP for a WebRTC Sink (e.g., a browser client):**

```sdp
v=0
o=- <sess-id> <sess-version> IN IP4 0.0.0.0
s=ScreamRouter WebRTC Sink
c=IN IP4 0.0.0.0
t=0 0
m=application <client-webrtc-port> DTLS/SCTP webrtc-datachannel
a=sctp-port:5000
a=max-message-size:262144
a=setup:actpass
a=mid:0
a=recvonly
a=x-screamrouter-role:sink
a=x-screamrouter-uuid:yyyyyyyy-yyyy-yyyy-yyyy-yyyyyyyyyyyy
a=ice-ufrag:xxxxxxxx
a=ice-pwd:xxxxxxxxxxxxxxxxxxxxxxxx
a=fingerprint:sha-256 XX:XX:XX:XX:XX:XX:XX:XX:XX:XX:XX:XX:XX:XX:XX:XX:XX:XX:XX:XX:XX:XX:XX:XX:XX:XX:XX:XX:XX:XX:XX:XX
```
(ICE candidates would also be listed)
