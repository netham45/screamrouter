# Sub-Task 2.6: Update Configuration for WebRTC Parameters (Python & C++)

**Objective:** Update Python Pydantic models and C++ structs to include WebRTC-specific configuration (STUN/TURN servers). Modify `ConfigurationManager` and `AudioEngineConfigApplier` to handle these new parameters.

**Parent Task:** [WebRTC Library (libdatachannel) Integration](../task_02_webrtc_library_integration.md)
**Previous Sub-Task:** [Sub-Task 2.5: Create Pybind11 Bindings for Signaling Bridge](./subtask_2.5_pybind11_bindings_signaling.md)

## Key Steps & Considerations:

1.  **Update Python Pydantic Models (`src/screamrouter_types/configuration.py`):**
    *   This aligns with `task_04_python_config_updates.md`.
    *   Ensure `protocol_type` Literal in `BaseDescription` (or `SourceDescription`/`SinkDescription`) includes "webrtc".
        ```python
        # In src/screamrouter_types/configuration.py
        # class BaseDescription(BaseModel):
        #    protocol_type: Literal["scream", "rtp", "webrtc", "sip"] = "scream" 
        ```
    *   Define `TURNServerConfig(BaseModel)` if not already present from `task_04`.
        ```python
        class TURNServerConfig(BaseModel):
            urls: List[str] = Field(default_factory=list)
            username: Optional[str] = None
            credential: Optional[str] = None
        ```
    *   Define `WebRTCConfig(BaseModel)`:
        ```python
        class WebRTCConfig(BaseModel):
            stun_servers: List[str] = Field(default_factory=lambda: ["stun:stun.l.google.com:19302"])
            turn_servers: List[TURNServerConfig] = Field(default_factory=list)
        ```
    *   Add `webrtc_config: Optional[WebRTCConfig] = Field(default_factory=WebRTCConfig)` to `SinkDescription` (as WebRTC is primarily for output sinks in this design). If bi-directional WebRTC is planned later, add to `SourceDescription` too.

2.  **Update C++ Configuration Structs (`src/configuration/audio_engine_config_types.h`):**
    *   Ensure `enum class ProtocolType` includes `WEBRTC`.
        ```cpp
        // enum class ProtocolType {
        //     LEGACY_SCREAM = 0,
        //     RTP = 1,
        //     WEBRTC = 2 // Add this
        //     // SIP_MANAGED will be added later
        // };
        ```
    *   Define `WebRTCTURNServerCpp` and `WebRTCConfigCpp` (as detailed in Sub-Task 2.2):
        ```cpp
        // struct WebRTCTURNServerCpp {
        //     std::vector<std::string> urls;
        //     std::string username;
        //     std::string credential;
        // };
        // struct WebRTCConfigCpp {
        //     std::vector<std::string> stun_servers;
        //     std::vector<WebRTCTURNServerCpp> turn_servers;
        // };
        ```
    *   Add `WebRTCConfigCpp webrtc_config;` to `SinkConfig`.

3.  **Update `ConfigurationManager` (`src/configuration/configuration_manager.py`):**
    *   **Loading/Saving:** Ensure `__load_config` and `__save_config` handle `protocol_type: "webrtc"` and the nested `webrtc_config`.
    *   **`_translate_config_to_cpp_desired_state`:**
        *   When creating C++ `SinkConfig`:
            *   If `desc.protocol_type == "webrtc"` and `desc.webrtc_config`:
                *   Set `cpp_config.protocol_type = ProtocolType::WEBRTC;`
                *   Populate `cpp_config.webrtc_config.stun_servers`.
                *   Populate `cpp_config.webrtc_config.turn_servers` by converting each Python `TURNServerConfig` to `WebRTCTURNServerCpp`.

4.  **Update `AudioEngineConfigApplier` (`src/configuration/audio_engine_config_applier.cpp`):**
    *   In `apply_state`, when processing `SinkConfig`:
        *   If `config.protocol_type == ProtocolType::WEBRTC`:
            *   Instruct `AudioManager` (which then instructs `SinkAudioMixer`) to use a `WebRTCSender`.
            *   Pass the `config.webrtc_config` (containing STUN/TURN info) to `WebRTCSender`'s initialization.

5.  **Update `AudioManager` (`src/audio_engine/audio_manager.cpp` and `.h`):**
    *   Modify `add_sink` (and related logic):
        *   When `SinkConfig::protocol_type` is `ProtocolType::WEBRTC`, ensure `SinkAudioMixer` is instantiated with the necessary information to create a `WebRTCSender`. This includes passing the `WebRTCConfigCpp` and the `AudioManager`'s WebRTC signaling callback mechanism.

6.  **Update `SinkAudioMixer` (`src/audio_engine/sink_audio_mixer.cpp` and `.h`):**
    *   Constructor/initializer now receives `WebRTCConfigCpp` if the protocol is WebRTC.
    *   When instantiating `WebRTCSender`, its `initialize` method will receive the `SinkConfig` (which contains `webrtc_config`) to set up `rtc::Configuration`.

## Code Alterations:

*   **`src/screamrouter_types/configuration.py`:**
    *   Add/update `protocol_type` Literal to include "webrtc".
    *   Define/ensure `TURNServerConfig` and `WebRTCConfig` Pydantic models.
    *   Add `webrtc_config` field to `SinkDescription`.
*   **`src/configuration/audio_engine_config_types.h`:**
    *   Add `WEBRTC` to `ProtocolType` enum.
    *   Define/ensure `WebRTCTURNServerCpp` and `WebRTCConfigCpp` structs.
    *   Add `webrtc_config` field to C++ `SinkConfig`.
*   **`src/configuration/configuration_manager.py`:**
    *   Update `_translate_config_to_cpp_desired_state` to handle `webrtc_config`.
*   **`src/configuration/audio_engine_config_applier.cpp`:**
    *   Update `apply_state` to handle `ProtocolType::WEBRTC` for sinks.
*   **`src/audio_engine/audio_manager.h` & `.cpp`:**
    *   Ensure `add_sink` logic correctly passes WebRTC config to `SinkAudioMixer`.
*   **`src/audio_engine/sink_audio_mixer.h` & `.cpp`:**
    *   Ensure constructor/initializer can accept and use `WebRTCConfigCpp` to configure `WebRTCSender`.
*   **`src/audio_engine/webrtc_sender.cpp`:**
    *   `initialize()` method uses the `SinkConfig::webrtc_config` to populate its internal `rtc::Configuration`.

## Recommendations:

*   **Default STUN Server:** Providing a default public STUN server (like `stun:stun.l.google.com:19302`) in `WebRTCConfig` is user-friendly.
*   **Security of TURN Credentials:** Be mindful if TURN credentials are ever logged or exposed unintentionally.
*   **UI Integration:** These configuration options will eventually need to be exposed in the web UI (covered in `task_06_ui_updates.md`).

## Acceptance Criteria:

*   Python and C++ configuration structures are updated to include `protocol_type: "webrtc"` and WebRTC-specific parameters (STUN/TURN servers).
*   `ConfigurationManager` correctly translates these WebRTC parameters for the C++ engine.
*   `AudioEngineConfigApplier` uses `protocol_type` to trigger WebRTC-specific setup in `AudioManager` for sinks.
*   `AudioManager` and `SinkAudioMixer` can instantiate and pass `WebRTCConfigCpp` to `WebRTCSender`.
*   `WebRTCSender` uses the provided STUN/TURN configuration when creating its `rtc::PeerConnection`.
*   The system can load and save configurations containing these new WebRTC settings.
