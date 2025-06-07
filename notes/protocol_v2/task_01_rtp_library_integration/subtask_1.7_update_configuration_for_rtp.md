# Sub-Task 1.7: Update Configuration for RTP Parameters (Python & C++)

**Objective:** Update the Python Pydantic models (`SourceDescription`, `SinkDescription`) and corresponding C++ structs (`SourceConfig`, `SinkConfig`) to include RTP-specific configuration parameters. Also, update `ConfigurationManager` and `AudioEngineConfigApplier` to handle these new parameters.

**Parent Task:** [RTP Library (oRTP) Integration](../task_01_rtp_library_integration.md)

## Key Steps & Considerations:

1.  **Update Python Pydantic Models (`src/screamrouter_types/configuration.py`):**
    *   This task aligns with parts of `task_04_python_config_updates.md` but focuses specifically on the fields immediately needed for basic RTP functionality with oRTP.
    *   Add `protocol_type: Literal["scream", "rtp"] = "scream"` to `BaseDescription` (or `SourceDescription` and `SinkDescription` individually). Initially, only "scream" and "rtp" are needed for this task. "webrtc" and "sip" will come later.
    *   Define `RTPConfig(BaseModel)`:
        ```python
        from pydantic import BaseModel, Field
        from typing import Optional, List, Literal

        class RTPConfig(BaseModel):
            # For Sinks (Output)
            destination_port: Optional[int] = Field(default=None, description="RTP destination port for sending.")
            # For Sources (Input)
            source_listening_port: Optional[int] = Field(default=None, description="RTP listening port for receiving.")
            
            payload_type_pcm: int = Field(default=127, description="RTP payload type for PCM audio. Dynamic type, e.g., 96-127.") # Default to a common dynamic type
            payload_type_mp3: int = Field(default=14, description="RTP payload type for MP3 audio. Often 14 or dynamic.") # Common for MP3
            # ssrc: Optional[int] = Field(default=None, description="Specific SSRC to use/expect. Auto-generated if None.") # SSRC might be auto-generated or set
            # codec_preferences: List[str] = Field(default_factory=list) # e.g., ["L16/48000/2", "MP3"] - for future SDP use
        ```
    *   Add `rtp_config: Optional[RTPConfig] = Field(default_factory=RTPConfig)` to `SourceDescription` and `SinkDescription`.

2.  **Update C++ Configuration Structs (`src/configuration/audio_engine_config_types.h`):**
    *   Define an enum for `ProtocolType`:
        ```cpp
        // In audio_engine_config_types.h
        enum class ProtocolType {
            LEGACY_SCREAM = 0,
            RTP = 1
            // WEBRTC will be added later
            // SIP_MANAGED will be added later
        };
        ```
    *   Add `ProtocolType protocol_type;` to `BaseConfig` (or `SourceConfig` and `SinkConfig`).
    *   Define `RTPConfigCpp` struct:
        ```cpp
        struct RTPConfigCpp {
            int destination_port = 0; // For sending
            int source_listening_port = 0; // For receiving
            uint8_t payload_type_pcm = 127;
            uint8_t payload_type_mp3 = 14;
            // uint32_t ssrc = 0; // If needed
        };
        ```
    *   Add `RTPConfigCpp rtp_config;` to `SourceConfig` and `SinkConfig`.

3.  **Update `ConfigurationManager` (`src/configuration/configuration_manager.py`):**
    *   **Loading/Saving:** Ensure `__load_config` and `__save_config` handle the new `protocol_type` and nested `rtp_config` in `config.yaml`. Pydantic's `model_dump` and parsing should manage this.
    *   **`_translate_config_to_cpp_desired_state`:**
        *   When creating C++ `SourceConfig` and `SinkConfig` instances:
            *   Set `cpp_config.protocol_type` based on Python `desc.protocol_type`.
            *   If `desc.protocol_type == "rtp"` and `desc.rtp_config` exists:
                *   Populate `cpp_config.rtp_config.destination_port` (for sinks) or `cpp_config.rtp_config.source_listening_port` (for sources).
                *   Populate `cpp_config.rtp_config.payload_type_pcm` and `mp3`.

4.  **Update `AudioEngineConfigApplier` (`src/configuration/audio_engine_config_applier.cpp`):**
    *   In `apply_state`, when processing `SourceConfig` and `SinkConfig`:
        *   Check `config.protocol_type`.
        *   If `ProtocolType::RTP`:
            *   For `SourceConfig`: Instruct `AudioManager` to create/configure an `RtpReceiver` using `config.rtp_config.source_listening_port` and other RTP params.
            *   For `SinkConfig`: Instruct `AudioManager` (which then instructs `SinkAudioMixer`) to use an `RTPSender`, configured with `config.ip`, `config.rtp_config.destination_port`, and other RTP params.
            *   This means `AudioManager` will need new methods or modified existing ones (e.g., `add_source`, `add_sink`) to accept and differentiate based on `protocol_type` and pass `RTPConfigCpp`.

5.  **Update `AudioManager` (`src/audio_engine/audio_manager.cpp` and `.h`):**
    *   Modify `add_source` / `remove_source`:
        *   If `SourceConfig::protocol_type` is `ProtocolType::RTP`:
            *   Instantiate `RtpReceiver` instead of `RawScreamReceiver` or `PerProcessScreamReceiver`.
            *   Pass necessary RTP parameters from `SourceConfig::rtp_config` to `RtpReceiver`'s constructor or an initialization method.
            *   Store it in a way that it can be managed (e.g., `std::map<std::string, std::unique_ptr<NetworkAudioReceiver>> network_audio_receivers_`).
    *   Modify `add_sink` / `remove_sink`:
        *   `SinkAudioMixer`'s constructor or an init method will need to receive the `protocol_type` and `RTPConfigCpp` (if RTP) from `SinkConfig`.
        *   `SinkAudioMixer` will then instantiate the correct `INetworkSender` (`RTPSender` in this case) as per `task_08_cpp_modular_sender.md`.

## Code Alterations:

*   **`src/screamrouter_types/configuration.py`:** Update Pydantic models.
*   **`src/configuration/audio_engine_config_types.h`:** Update C++ structs and enum.
*   **`src/configuration/configuration_manager.py`:** Update translation logic.
*   **`src/configuration/audio_engine_config_applier.cpp`:** Update `apply_state` logic.
*   **`src/audio_engine/audio_manager.h` & `src/audio_engine/audio_manager.cpp`:**
    *   Modify methods for adding/removing sources and sinks to handle `protocol_type` and pass RTP config.
*   **`src/audio_engine/rtp_receiver.h` & `src/audio_engine/rtp_receiver.cpp`:**
    *   Constructor/initializer now takes RTP specific parameters from `SourceConfig::rtp_config`.
    *   `default_audio_format_.rtp_payload_type` is initialized from this config.
*   **`src/audio_engine/rtp_sender.h` & `src/audio_engine/rtp_sender.cpp`:**
    *   `initialize()` method takes RTP specific parameters from `SinkConfig::rtp_config`.
    *   `pcm_payload_type_` and `mp3_payload_type_` are initialized.
*   **`src/audio_engine/sink_audio_mixer.h` & `src/audio_engine/sink_audio_mixer.cpp`:**
    *   Constructor/initializer now receives `protocol_type` and `RTPConfigCpp` to correctly instantiate `RTPSender`.

## Recommendations:

*   **Default Values:** Ensure sensible default values for new configuration parameters in both Python and C++.
*   **Backward Compatibility:** When loading `config.yaml`, older configurations without `protocol_type` or `rtp_config` should default to "scream" behavior. Pydantic handles this well with default values.
*   **Clarity in `AudioManager`:** The logic in `AudioManager` for creating different types of receivers/senders based on `protocol_type` should be clear and maintainable.
*   **Phased Rollout:** This sub-task focuses on the basic RTP parameters. More advanced settings (like SRTP, specific codec parameters beyond payload type) can be added in later iterations.

## Acceptance Criteria:

*   Python and C++ configuration structures are updated with `protocol_type` and basic RTP parameters.
*   `ConfigurationManager` correctly translates these parameters for the C++ engine.
*   `AudioEngineConfigApplier` uses `protocol_type` to trigger RTP-specific setup in `AudioManager`.
*   `AudioManager` can instantiate and configure `RtpReceiver` and `RTPSender` (via `SinkAudioMixer`) using the new RTP configuration parameters.
*   The system can load and save configurations containing these new RTP settings.
