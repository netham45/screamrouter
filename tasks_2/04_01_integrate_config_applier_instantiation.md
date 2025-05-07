# Task 04_01: Integrate `AudioEngineConfigApplier` Instantiation in `ConfigurationManager.py`

**Objective:** Modify `ConfigurationManager.py` to import the necessary components from the C++ extension module and instantiate the `AudioManager` and `AudioEngineConfigApplier` C++ objects during its initialization.

**File to Modify:** `src/configuration/configuration_manager.py`

**Details:**

1.  **Import C++ Extension Module:**
    *   At the top of the file, add an import statement for the compiled C++ extension. The name should match the first argument of the `PYBIND11_MODULE` macro in `bindings.cpp`.
    ```python
    # Assuming the module name is screamrouter_audio_engine
    import screamrouter_audio_engine 
    # It might be prudent to add error handling for the import
    # try:
    #     import screamrouter_audio_engine
    # except ImportError:
    #     _logger.critical("Failed to import C++ audio engine module. Ensure it's compiled correctly.")
    #     # Decide how to handle this - exit? run without C++ engine?
    #     screamrouter_audio_engine = None # Placeholder to prevent later errors if continuing
    ```

2.  **Instantiate C++ Objects in `__init__`:**
    *   Locate the `__init__` method of the `ConfigurationManager` class.
    *   Add new member variables to hold the C++ objects.
    *   Instantiate `screamrouter_audio_engine.AudioManager`.
    *   Initialize the `AudioManager` (e.g., call its `initialize` method). Handle potential initialization failures.
    *   Instantiate `screamrouter_audio_engine.AudioEngineConfigApplier`, passing the `AudioManager` instance to its constructor.
    *   Store these instances as member variables (e.g., `self.cpp_audio_manager`, `self.cpp_config_applier`).

    ```python
    # Inside ConfigurationManager.__init__

        # ... (other initializations like self.mdns_responder, etc.) ...

        self.cpp_audio_manager: Optional[screamrouter_audio_engine.AudioManager] = None
        self.cpp_config_applier: Optional[screamrouter_audio_engine.AudioEngineConfigApplier] = None

        if screamrouter_audio_engine: # Check if import succeeded
            try:
                _logger.info("[Configuration Manager] Initializing C++ AudioManager...")
                self.cpp_audio_manager = screamrouter_audio_engine.AudioManager()
                
                # Determine the RTP listen port (e.g., from constants or config)
                rtp_listen_port = constants.RTP_LISTEN_PORT # Assuming a constant exists
                
                if not self.cpp_audio_manager.initialize(rtp_listen_port):
                    _logger.error("[Configuration Manager] Failed to initialize C++ AudioManager.")
                    # Handle failure - maybe set self.cpp_audio_manager back to None?
                    self.cpp_audio_manager = None 
                else:
                    _logger.info("[Configuration Manager] C++ AudioManager initialized successfully on port %d.", rtp_listen_port)
                    # Only create applier if manager initialized successfully
                    self.cpp_config_applier = screamrouter_audio_engine.AudioEngineConfigApplier(self.cpp_audio_manager)
                    _logger.info("[Configuration Manager] C++ AudioEngineConfigApplier created.")

            except Exception as e:
                _logger.exception("[Configuration Manager] Exception during C++ engine initialization: %s", e)
                self.cpp_audio_manager = None # Ensure it's None on error
                self.cpp_config_applier = None

        # ... (rest of __init__, like loading config, starting thread) ...
        
        # Ensure __load_config happens AFTER potential C++ engine init attempt
        self.__load_config() 
        
        # ... start self thread ...
    ```
    *Self-correction: Added check for successful import, error handling for initialization, and ensured applier is only created if manager init succeeds. Added RTP port configuration.*

3.  **Handle Shutdown in `stop` Method:**
    *   Locate the `stop` method of `ConfigurationManager`.
    *   Add calls to shut down the C++ `AudioManager` *before* stopping other components that might depend on it (like Python `AudioController`s if they are kept).
    *   Ensure the shutdown happens gracefully and handles cases where the C++ objects might not have been initialized successfully.

    ```python
    # Inside ConfigurationManager.stop

        # ... (stop webstream, etc.) ...

        # Shutdown C++ Engine first
        if self.cpp_audio_manager:
            try:
                _logger.info("[Configuration Manager] Shutting down C++ AudioManager...")
                self.cpp_audio_manager.shutdown()
                _logger.info("[Configuration Manager] C++ AudioManager shutdown complete.")
            except Exception as e:
                _logger.exception("[Configuration Manager] Exception during C++ AudioManager shutdown: %s", e)
        
        # Now stop Python receivers/controllers etc.
        _logger.debug("[Configuration Manager] Stopping receiver")
        self.scream_recevier.stop() 
        # ... (stop other Python components) ...
        
        _logger.debug("[Configuration Manager] Stopping audio controllers")
        # If Python AudioControllers are kept, stop them AFTER C++ engine
        for audio_controller in self.audio_controllers:
             audio_controller.stop()
        _logger.debug("[Configuration Manager] Audio controllers stopped")

        # ... (stop mDNS, set self.running = False, join thread) ...
    ```
    *Self-correction: Moved C++ AudioManager shutdown earlier in the sequence.*

**Acceptance Criteria:**

*   `ConfigurationManager.py` imports the `screamrouter_audio_engine` module.
*   The `__init__` method attempts to create and initialize `screamrouter_audio_engine.AudioManager`.
*   If `AudioManager` initialization succeeds, `__init__` creates `screamrouter_audio_engine.AudioEngineConfigApplier`, passing the `AudioManager` instance.
*   Instances are stored in member variables (e.g., `self.cpp_audio_manager`, `self.cpp_config_applier`).
*   Error handling is added for import and initialization failures.
*   The `stop` method calls `self.cpp_audio_manager.shutdown()` if the manager exists, before stopping dependent Python components.
