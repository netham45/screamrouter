# Task 8: Update Bindings

**Goal**: Expose the new `RawScreamReceiver` management capabilities and configuration options to the Python layer via pybind11.

**Files to Modify**:
*   `src/audio_engine/bindings.cpp`

**Steps**:

1.  **Bind `RawScreamReceiverConfig`**:
    *   Add a `py::class_` definition for `RawScreamReceiverConfig`.
    ```cpp
    py::class_<RawScreamReceiverConfig>(m, "RawScreamReceiverConfig", "Configuration for a raw Scream receiver")
        .def(py::init<>()) // Default constructor
        .def_readwrite("listen_port", &RawScreamReceiverConfig::listen_port, "UDP port to listen on");
        // Add .def_readwrite("bind_ip", ...) if that field is included
    ```

2.  **Bind `InputProtocolType` Enum (Optional but Recommended)**:
    *   Bind the `InputProtocolType` enum to make it directly usable in Python.
    ```cpp
    py::enum_<InputProtocolType>(m, "InputProtocolType", "Specifies the expected input packet type")
        .value("RTP_SCREAM_PAYLOAD", InputProtocolType::RTP_SCREAM_PAYLOAD)
        .value("RAW_SCREAM_PACKET", InputProtocolType::RAW_SCREAM_PACKET)
        .export_values(); // Make enum values accessible as module attributes
    ```
    *   *(Note: If binding the enum causes issues or is overly complex for the build system, stick with the integer hint in `SourceConfig`)*.

3.  **Update `SourceConfig` Binding**:
    *   Add the `protocol_type_hint` field.
    *   Add the `target_receiver_port` field (added in Task 7's self-correction).
    ```cpp
    py::class_<SourceConfig>(m, "SourceConfig", "Configuration for an audio source")
        // ... existing .def_readwrite lines ...
        .def_readwrite("protocol_type_hint", &SourceConfig::protocol_type_hint, "Input protocol type (0=RTP_PAYLOAD, 1=RAW_PACKET)")
        .def_readwrite("target_receiver_port", &SourceConfig::target_receiver_port, "Target receiver listen port (used for RAW_PACKET type)");
    ```

4.  **Bind New `AudioManager` Methods**:
    *   Add `.def()` calls for `add_raw_scream_receiver` and `remove_raw_scream_receiver` to the `py::class_<AudioManager>` definition.
    ```cpp
    py::class_<AudioManager>(m, "AudioManager", "Main class for managing the C++ audio engine")
        // ... existing constructor and methods ...

        // Raw Scream Receiver Management
        .def("add_raw_scream_receiver", &AudioManager::add_raw_scream_receiver,
             py::arg("config"),
             "Adds and starts a new raw Scream receiver. Returns true on success.")
        .def("remove_raw_scream_receiver", &AudioManager::remove_raw_scream_receiver,
             py::arg("listen_port"),
             "Stops and removes the raw Scream receiver listening on the given port. Returns true on success.")

        // ... rest of existing methods ...
        ;
