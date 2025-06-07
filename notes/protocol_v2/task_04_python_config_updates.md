# Task: Python Configuration System Updates for Protocol v2

**Objective:** Update the Python-based configuration system (`ConfigurationManager`, Pydantic models, and YAML persistence) to support the new fields and logic required for Protocol v2, including SIP-based device management and multiple protocol types.

**Parent Plan Section:** VI. Configuration System & UI Updates (Python-specific parts)

**Files to Modify/Create:**

*   **`src/screamrouter_types/configuration.py`:**
    *   Modify `SourceDescription` and `SinkDescription` Pydantic models:
        *   Add `uuid: Optional[str] = Field(default=None, description="Universally Unique Identifier for the device")`
        *   Add `protocol_type: Literal["scream", "rtp", "webrtc", "sip"] = Field(default="scream", description="Primary communication protocol")`
        *   Add `sip_contact_uri: Optional[str] = Field(default=None, description="SIP Contact URI of the registered device")`
        *   Add `rtp_config: Optional[RTPConfig] = Field(default_factory=lambda: RTPConfig(), description="RTP specific parameters")`
        *   Add `webrtc_config: Optional[WebRTCConfig] = Field(default_factory=lambda: WebRTCConfig(), description="WebRTC specific parameters")`
    *   Define new Pydantic models for protocol-specific configurations:
        *   `TURNServerConfig(BaseModel)`:
            *   `urls: List[str] = []`
            *   `username: Optional[str] = None`
            *   `credential: Optional[str] = None`
        *   `RTPConfig(BaseModel)`:
            *   `destination_port: Optional[int] = None` (For RTP data, distinct from SIP port, if sink)
            *   `payload_type_pcm: Optional[int] = 127` # Default to a Scream-like dynamic type
            *   `payload_type_mp3: Optional[int] = 14`  # Common dynamic type for MP3
            *   `codec_preferences: List[str] = Field(default_factory=list)` # e.g., ["L16/48000/2", "MP3/192k"]
            *   `srtp_enabled: bool = False` # Future: for SRTP
            *   `srtp_key: Optional[str] = None` # Future: for SRTP
        *   `WebRTCConfig(BaseModel)`:
            *   `stun_servers: List[str] = Field(default_factory=lambda: ["stun:stun.l.google.com:19302"])` # Example default
            *   `turn_servers: List[TURNServerConfig] = Field(default_factory=list)`
*   **`src/configuration/configuration_manager.py`:**
    *   **Loading/Saving (`__load_config`, `__save_config`):**
        *   Update YAML loading/saving to handle the new fields in `SourceDescription` and `SinkDescription` (including nested `RTPConfig`, `WebRTCConfig`). Use `model_dump(exclude_none=True)` for saving to keep YAML clean.
        *   Ensure backward compatibility when loading older `config.yaml` files (Pydantic defaults will handle missing fields).
    *   **SIP Integration Methods:**
        *   Implement `handle_sip_registration(self, uuid: str, client_ip: str, client_port: int, sip_contact_uri: str, client_role: str, sdp_capabilities: dict) -> bool`:
            *   Checks if a source/sink with `uuid` exists.
            *   If yes: Updates its `ip` (if changed, or if it's the primary IP for the device), `port` (SIP port), `sip_contact_uri`, `protocol_type` (to "sip", or "rtp"/"webrtc" based on SDP media lines), and other capabilities derived from `sdp_capabilities` (e.g., populate `rtp_config.codec_preferences`, `rtp_config.destination_port` if specified in SDP for RTP media). Marks as `enabled = True`.
            *   If no: Creates a new `SourceDescription` or `SinkDescription`. `name` can be `f"{client_role}_{uuid[:8]}"`. `ip` is `client_ip`. `port` is `client_port`.
            *   Triggers `self.__reload_configuration()` and `self.__save_config()`.
        *   Implement `handle_sip_device_offline(self, uuid: str) -> bool`:
            *   Finds the source/sink by `uuid`.
            *   Marks it as `enabled = False`.
            *   Triggers `self.__reload_configuration()` and `self.__save_config()`.
        *   Implement `get_device_config_by_uuid(self, uuid: str) -> Optional[Union[SourceDescription, SinkDescription]]`.
    *   **C++ Engine State Translation (`_translate_config_to_cpp_desired_state`):**
        *   When creating C++ `SourceConfig` and `SinkConfig` (from `audio_engine_config_types.h`):
            *   Pass the `protocol_type` (as an int/enum understandable by C++).
            *   If `protocol_type` is RTP:
                *   Pass `sink_desc.ip` (or source_desc.ip) as the RTP destination/source IP.
                *   Pass `sink_desc.rtp_config.destination_port` as the RTP port.
                *   Pass other relevant RTP params from `rtp_config` if the C++ structs are updated (e.g., payload types, SRTP info).
            *   If `protocol_type` is WebRTC:
                *   The primary interaction will be signaling. C++ `SinkConfig` might just need to know it's WebRTC. STUN/TURN info is usually handled by `libdatachannel` itself, possibly configured globally or via signaling.
*   **`src/configuration/audio_engine_config_types.h` (C++ side):**
    *   The C++ `SinkConfig` struct will need to be updated to store `protocol_type` (as an enum).
    *   It will also need fields for RTP specific destination info if it's an RTP sink (e.g., `std::string rtp_destination_ip; int rtp_destination_port;`).
    *   `SourceConfig` might need similar if sources can be RTP.
*   **`src/configuration/audio_engine_config_applier.cpp`:**
    *   `AudioEngineConfigApplier::apply_state` will use the `protocol_type` from the C++ `SinkConfig`/`SourceConfig` to determine how to configure the `AudioManager` (e.g., tell it to set up an RTP sender for a sink, or an RTP receiver path for a source).

**Detailed Steps:**

1.  **Update Pydantic Models (`src/screamrouter_types/configuration.py`):**
    *   Add `uuid`, `protocol_type`, `sip_contact_uri`, `rtp_config`, `webrtc_config` to `SourceDescription` and `SinkDescription`.
    *   Define `RTPConfig`, `WebRTCConfig`, `TURNServerConfig`.
2.  **Update `ConfigurationManager` - Persistence & SIP Logic:**
    *   Modify `__load_config` and `__save_config`.
    *   Implement `handle_sip_registration`, `handle_sip_device_offline`, `get_device_config_by_uuid`.
3.  **Update C++ Structs & `ConfigurationManager` C++ Translation:**
    *   Modify C++ `SourceConfig` and `SinkConfig` in `audio_engine_config_types.h` to include `protocol_type` and necessary RTP/WebRTC params.
    *   Update `_translate_config_to_cpp_desired_state` in `ConfigurationManager` to populate these new C++ struct fields.
4.  **Update `AudioEngineConfigApplier`:**
    *   Modify `apply_state` to interpret `protocol_type` and call appropriate `AudioManager` setup functions for different protocols. This might involve `AudioManager` having new methods like `add_rtp_sink_path` or similar, distinct from the current generic `add_sink` which implies legacy Scream.
5.  **Testing:**
    *   YAML load/save tests.
    *   Unit tests for SIP registration/offline handling.
    *   Verify C++ translation passes protocol types.
    *   Integration test with `SipManager` to ensure `config.yaml` updates correctly.

**Acceptance Criteria:**

*   `config.yaml` correctly stores/loads all new fields including nested protocol configs.
*   `ConfigurationManager` can dynamically manage device entries based on SIP events.
*   C++ audio engine receives correct protocol type and parameters.
