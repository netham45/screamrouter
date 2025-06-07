# Sub-Task 4.3: Update C++ Config Structs and `ConfigurationManager` Translation

**Objective:** Ensure C++ configuration structs (`SourceConfig`, `SinkConfig`, `RTPConfigCpp`, `WebRTCConfigCpp`) in `src/configuration/audio_engine_config_types.h` are finalized to match the Pydantic models. Update the `_translate_config_to_cpp_desired_state` method in `ConfigurationManager` to correctly map all new Python Pydantic model fields to their C++ counterparts.

**Parent Task:** [Python Configuration System Updates for Protocol v2](../task_04_python_config_updates.md)
**Previous Sub-Task:** [Sub-Task 4.2: Update `ConfigurationManager` for Persistence and SIP Event Handling](./subtask_4.2_configmanager_persistence_sip_logic.md)

## Key Steps & Considerations:

1.  **Finalize C++ Structs (`src/configuration/audio_engine_config_types.h`):**
    *   **`ProtocolType` Enum:**
        ```cpp
        enum class ProtocolType {
            LEGACY_SCREAM = 0,
            RTP = 1,
            WEBRTC = 2,
            SIP_MANAGED = 3 // Represents a device whose config is largely managed by SIP state
        };
        ```
    *   **`RTPConfigCpp` Struct:**
        ```cpp
        struct RTPConfigCpp {
            int destination_port = 0;       // For Sinks
            int source_listening_port = 0;  // For Sources
            uint8_t payload_type_pcm = 127;
            uint8_t payload_type_mp3 = 14;
            // std::vector<std::string> codec_preferences; // If needed by C++ directly
            // bool srtp_enabled = false;
            // std::string srtp_key;
        };
        ```
    *   **`WebRTCTURNServerCpp` Struct:**
        ```cpp
        struct WebRTCTURNServerCpp {
            std::vector<std::string> urls;
            std::string username;
            std::string credential;
        };
        ```
    *   **`WebRTCConfigCpp` Struct:**
        ```cpp
        struct WebRTCConfigCpp {
            std::vector<std::string> stun_servers;
            std::vector<WebRTCTURNServerCpp> turn_servers;
        };
        ```
    *   **`BaseConfigCpp` (Conceptual, or add fields directly to `SourceConfig`/`SinkConfig`):**
        *   `std::string uuid;`
        *   `ProtocolType protocol_type = ProtocolType::LEGACY_SCREAM;`
        *   `std::string sip_contact_uri;`
        // `bool online_status;` // If separating from `enabled`
        // `long long last_seen_timestamp_ms;` // If needed in C++
    *   **`SourceConfig` and `SinkConfig`:**
        *   Ensure they include the fields from `BaseConfigCpp` (either via inheritance or direct members).
        *   `RTPConfigCpp rtp_config;`
        *   `WebRTCConfigCpp webrtc_config;` (Primarily for `SinkConfig`, but can be in `SourceConfig` if future bi-directional WebRTC sources are envisioned).

2.  **Update `ConfigurationManager._translate_config_to_cpp_desired_state`:**
    *   **File:** `src/configuration/configuration_manager.py`
    *   This method iterates through Python `SourceDescription` and `SinkDescription` objects and creates corresponding C++ `SourceConfig` and `SinkConfig` objects (or data structures that will be used to populate them via pybind11).
    *   **Mapping `protocol_type`:**
        ```python
        # Example for protocol_type mapping
        # protocol_type_map = {
        #    "scream": 0, # audio_engine_python.ProtocolType.LEGACY_SCREAM
        #    "rtp": 1,    # audio_engine_python.ProtocolType.RTP
        #    "webrtc": 2, # audio_engine_python.ProtocolType.WEBRTC
        #    "sip": 3     # audio_engine_python.ProtocolType.SIP_MANAGED
        # }
        # cpp_source_config.protocol_type = protocol_type_map.get(py_source_desc.protocol_type, 0)
        ```
        It's better to use the actual enum values exposed by the pybind11 module if possible, e.g., `self.audio_engine_client.ProtocolType.RTP`.
    *   **Mapping `uuid` and `sip_contact_uri`:**
        ```python
        # cpp_source_config.uuid = py_source_desc.uuid if py_source_desc.uuid else ""
        # cpp_source_config.sip_contact_uri = py_source_desc.sip_contact_uri if py_source_desc.sip_contact_uri else ""
        ```
    *   **Mapping `rtp_config`:**
        ```python
        # if py_source_desc.protocol_type == "rtp" and py_source_desc.rtp_config:
        #    cpp_source_config.rtp_config.source_listening_port = py_source_desc.rtp_config.source_listening_port or 0
        #    cpp_source_config.rtp_config.payload_type_pcm = py_source_desc.rtp_config.payload_type_pcm
        #    cpp_source_config.rtp_config.payload_type_mp3 = py_source_desc.rtp_config.payload_type_mp3
        # For sinks:
        # if py_sink_desc.protocol_type == "rtp" and py_sink_desc.rtp_config:
        #    cpp_sink_config.rtp_config.destination_port = py_sink_desc.rtp_config.destination_port or 0
        #    # ... other rtp_config fields
        ```
    *   **Mapping `webrtc_config` (for Sinks):**
        ```python
        # if py_sink_desc.protocol_type == "webrtc" and py_sink_desc.webrtc_config:
        #    cpp_sink_config.webrtc_config.stun_servers = list(py_sink_desc.webrtc_config.stun_servers)
        #    cpp_turn_servers = []
        #    for py_turn in py_sink_desc.webrtc_config.turn_servers:
        #        cpp_turn = self.audio_engine_client.WebRTCTURNServerCpp() # Assuming constructor exposed
        #        cpp_turn.urls = list(py_turn.urls)
        #        cpp_turn.username = py_turn.username if py_turn.username else ""
        #        cpp_turn.credential = py_turn.credential if py_turn.credential else ""
        #        cpp_turn_servers.append(cpp_turn)
        #    cpp_sink_config.webrtc_config.turn_servers = cpp_turn_servers
        ```
    *   Ensure all relevant fields from the finalized Pydantic models are translated.

3.  **Update Pybind11 Bindings for C++ Structs (`src/audio_engine/bindings.cpp`):**
    *   If not already done, expose `ProtocolType` enum and the C++ config structs (`RTPConfigCpp`, `WebRTCTURNServerCpp`, `WebRTCConfigCpp`, `SourceConfig`, `SinkConfig`) to Python. This allows `ConfigurationManager` to create and populate these structs directly.
    ```cpp
    // Example in bindings.cpp
    // py::enum_<ProtocolType>(m, "ProtocolType")
    //     .value("LEGACY_SCREAM", ProtocolType::LEGACY_SCREAM)
    //     .value("RTP", ProtocolType::RTP)
    //     .value("WEBRTC", ProtocolType::WEBRTC)
    //     .value("SIP_MANAGED", ProtocolType::SIP_MANAGED)
    //     .export_values();

    // py::class_<RTPConfigCpp>(m, "RTPConfigCpp")
    //     .def(py::init<>())
    //     .def_readwrite("destination_port", &RTPConfigCpp::destination_port)
    //     // ... other fields

    // py::class_<WebRTCTURNServerCpp>(m, "WebRTCTURNServerCpp")
    //     .def(py::init<>())
    //     // ... fields

    // py::class_<WebRTCConfigCpp>(m, "WebRTCConfigCpp")
    //     .def(py::init<>())
    //     // ... fields

    // py::class_<SourceConfig>(m, "SourceConfig") // Assuming SourceConfig is a distinct struct
    //     .def(py::init<>())
    //     .def_readwrite("protocol_type", &SourceConfig::protocol_type)
    //     .def_readwrite("uuid", &SourceConfig::uuid)
    //     .def_readwrite("rtp_config", &SourceConfig::rtp_config)
    //     // ... other fields
    ```

## Code Alterations:

*   **`src/configuration/audio_engine_config_types.h`:**
    *   Finalize the C++ structs and `ProtocolType` enum as detailed above.
*   **`src/configuration/configuration_manager.py`:**
    *   Update `_translate_config_to_cpp_desired_state` to perform comprehensive mapping from the finalized Pydantic models to the C++ structs, including all new fields for UUID, SIP info, RTP, and WebRTC configurations.
*   **`src/audio_engine/bindings.cpp`:**
    *   Ensure all necessary C++ configuration structs and enums are exposed to Python via pybind11.

## Recommendations:

*   **Consistency:** Maintain strict consistency between Pydantic field names/types and their C++ counterparts to avoid confusion and errors during translation.
*   **Default Values in C++:** Ensure C++ structs also have sensible default values, especially for fields that might not always be provided by the Python side (though Pydantic defaults should handle most cases before translation).
*   **Error Handling in Translation:** Add logging or error handling in `_translate_config_to_cpp_desired_state` if unexpected data types or missing required fields (that should have defaults) are encountered.
*   **Testing Translation:** After implementation, specifically test the translation logic by creating various Python configurations (e.g., RTP source, WebRTC sink, SIP-managed device) and verifying that the C++ structs are populated correctly by inspecting logs or debugger output.

## Acceptance Criteria:

*   C++ configuration structs in `audio_engine_config_types.h` are complete and match the finalized Pydantic models for Protocol v2.
*   `ConfigurationManager._translate_config_to_cpp_desired_state` correctly maps all relevant fields from Python Pydantic models to C++ structs, including `uuid`, `protocol_type`, `sip_contact_uri`, and nested `rtp_config` and `webrtc_config`.
*   Pybind11 bindings expose all necessary C++ structs and enums to Python for the translation process.
*   The translation process handles different protocol types correctly, populating the appropriate C++ config substructures.
