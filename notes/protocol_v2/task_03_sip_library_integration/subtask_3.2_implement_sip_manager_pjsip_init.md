# Sub-Task 3.2: Implement `SipManager` Class and PJSIP Initialization

**Objective:** Define the `SipManager` Python class structure, including initialization of the PJSIP `Endpoint`, creation of a SIP transport (UDP), and basic PJSIP library lifecycle management.

**Parent Task:** [SIP Library (pjproject) Integration](../task_03_sip_library_integration.md)
**Previous Sub-Task:** [Sub-Task 3.1: Add `pjproject` Dependency and System Setup](./subtask_3.1_pjproject_dependency_setup.md)

## Key Steps & Considerations:

1.  **Create `src/sip_server/sip_manager.py`:**
    *   This new file will house the `SipManager` class and related PJSIP logic.
    *   Import necessary modules: `pjsua2 as pj`, `threading`, `logging` (or `screamrouter_logger`).

2.  **`SipManager` Class Definition:**
    ```python
    import pjsua2 as pj
    import threading
    import time # For potential thread join timeouts
    # from screamrouter_logger import get_logger # Assuming a logger setup
    # logger = get_logger(__name__)
    import logging # Using standard logging for simplicity here
    logger = logging.getLogger(__name__)


    class SipManager:
        def __init__(self, config_manager, sip_port=5060, sip_domain="screamrouter.local"):
            self.ep = pj.Endpoint()
            self.lib_inited = False
            self.thread = None
            self.is_running = False
            self.config_manager = config_manager # To interact with ConfigurationManager
            self.sip_port = sip_port
            self.sip_domain = sip_domain # Used for constructing SIP URIs

            self.accounts = {} # To store pj.Account instances, perhaps keyed by UUID
            self.active_calls = {} # To store pj.Call instances for WebRTC signaling

        def start(self):
            if self.is_running:
                logger.info("SipManager already running.")
                return True

            try:
                logger.info("Starting SipManager...")
                self.ep.libCreate()
                self.lib_inited = True

                # Init library
                ep_cfg = pj.EpConfig()
                # ep_cfg.logConfig.level = 4 # Example: Set PJSIP log level
                # ep_cfg.uaConfig.threadCnt = 1 # Manage thread count
                # ep_cfg.uaConfig.mainThreadOnly = False # If running PJSIP event loop in its own thread
                self.ep.libInit(ep_cfg)

                # Create and start UDP transport
                transport_cfg = pj.TransportConfig()
                transport_cfg.port = self.sip_port
                self.ep.transportCreate(pj.PJSIP_TRANSPORT_UDP, transport_cfg)
                # TODO: Add TCP transport if needed later

                self.ep.libStart()
                logger.info(f"PJSIP library started. SIP UDP transport listening on port {self.sip_port}")

                # Create a default account to handle incoming REGISTER, INVITE etc.
                # This account won't register anywhere but will process incoming requests.
                # Details of this account (SipAccount class) will be in the next sub-task.
                # self.default_account = SipAccount(self, self.config_manager) 
                # acc_cfg = pj.AccountConfig()
                # acc_cfg.idUri = f"sip:{self.sip_domain}" # A local URI for the server
                # self.default_account.create(acc_cfg)

                self.is_running = True
                # PJSIP often runs its own worker threads. If ep_cfg.uaConfig.mainThreadOnly = True,
                # then pj.Endpoint.instance().libHandleEvents() would need to be called periodically.
                # If mainThreadOnly = False (default or explicit), PJSIP handles its events.
                # For a server, it's common to let PJSIP manage its threads.
                # A separate thread for SipManager's own periodic tasks (like checking timeouts) might still be useful.
                # self.thread = threading.Thread(target=self._run_periodic_tasks, daemon=True)
                # self.thread.start()

                logger.info("SipManager started successfully.")
                return True

            except pj.Error as e:
                logger.error(f"PJSIP Error in SipManager start: {e.info()}")
                self.stop() # Attempt cleanup
                return False
            except Exception as e:
                logger.error(f"Unexpected error in SipManager start: {e}")
                self.stop()
                return False

        def stop(self):
            logger.info("Stopping SipManager...")
            self.is_running = False

            # if self.thread and self.thread.is_alive():
            #     self.thread.join(timeout=5.0) # Wait for periodic task thread

            # PJSIP cleanup order is important
            try:
                # Destroy accounts, calls first if managed explicitly
                # for acc_uuid, acc in list(self.accounts.items()):
                #     # acc.delete() or similar, then del self.accounts[acc_uuid]
                # for call_id, call_obj in list(self.active_calls.items()):
                #     # call_obj.hangup() or similar, then del self.active_calls[call_id]
                
                # if hasattr(self, 'default_account') and self.default_account:
                #    del self.default_account # Deleting the pj.Account object

                if self.lib_inited: # Check if libInit was successful
                    # Destroy transports (optional, libDestroy will do it but can be explicit)
                    # self.ep.transportCloseAll()
                    
                    logger.info("Destroying PJSIP library...")
                    self.ep.libDestroy() # This should be called before ep is deleted
                    self.lib_inited = False
                
                # pj.Endpoint instance is deleted when self.ep goes out of scope or via `del self.ep`
                # but ensure libDestroy is called first.
            except pj.Error as e:
                logger.error(f"PJSIP Error during SipManager stop: {e.info()}")
            except Exception as e:
                logger.error(f"Unexpected error during SipManager stop: {e}")
            finally:
                # Ensure self.ep is cleaned up if it holds resources after libDestroy
                # This might not be strictly necessary if Python's GC handles it after libDestroy
                if hasattr(self, 'ep'):
                    del self.ep 
                logger.info("SipManager stopped.")
        
        # def _run_periodic_tasks(self):
        #     while self.is_running:
        #         # Example: Check for device timeouts
        #         # self.check_device_timeouts()
        #         time.sleep(5) # Check every 5 seconds
    ```

3.  **PJSIP Endpoint (`pj.Endpoint`):**
    *   A single instance of `pj.Endpoint` is created and managed by `SipManager`.
    *   `libCreate()`: Creates the underlying PJSIP library resources.
    *   `libInit(ep_cfg)`: Initializes the library with configurations (logging, threading, User-Agent).
    *   `transportCreate()`: Sets up network transports (UDP on `sip_port`). TCP can be added similarly if needed.
    *   `libStart()`: Starts the PJSIP library services and worker threads.
    *   `libDestroy()`: Destroys the library; must be called before the `Endpoint` object is deleted.

4.  **Threading Model:**
    *   PJSIP can manage its own worker threads for network events and timers if `EpConfig.uaConfig.mainThreadOnly` is `False` (default). This is generally suitable for a server application.
    *   `SipManager` might have its own thread for application-level periodic tasks (e.g., checking for inactive devices), separate from PJSIP's internal threads.

5.  **Integration with `ConfigurationManager`:**
    *   `SipManager` will take `ConfigurationManager` as a constructor argument to:
        *   Notify `ConfigurationManager` about new registrations or device status changes.
        *   Potentially query `ConfigurationManager` for device-specific SIP configurations if needed.

## Code Alterations:

*   **New File:** `src/sip_server/__init__.py` (if it doesn't exist, to make `sip_server` a package).
*   **New File:** `src/sip_server/sip_manager.py`
    *   Implement the `SipManager` class structure as sketched above.
    *   Focus on PJSIP initialization (`libCreate`, `libInit`, `transportCreate`, `libStart`) and shutdown (`libDestroy`).
    *   The actual `SipAccount` subclass and its callback implementations (`on_reg_state`, `on_incoming_call`) will be detailed in the next sub-task.
*   **File:** `screamrouter.py` (or main application entry point)
    *   Instantiate `SipManager`.
    *   Call `sip_manager.start()` during application startup.
    *   Ensure `sip_manager.stop()` is called during application shutdown (e.g., in a `finally` block or via `atexit`).
*   **File:** `src/configuration/configuration_manager.py`
    *   Will instantiate `SipManager`, passing `self` (the `ConfigurationManager` instance) to it.

## Recommendations:

*   **PJSIP Logging:** Configure PJSIP's own logging (`EpConfig.logConfig`) to an appropriate level during development (e.g., level 4 or 5 for details) and potentially to a file. This is invaluable for debugging SIP issues.
*   **Error Handling:** PJSIP methods often throw `pj.Error`. Wrap PJSIP calls in `try...except pj.Error as e:` blocks and log `e.info()`.
*   **PJSIP Documentation:** The PJSIP Book and `pjsua2` reference are essential resources for understanding PJSIP concepts and API usage.
*   **Lifecycle Management:** Ensure `libDestroy()` is always called before the `SipManager` (and its `pj.Endpoint` member) is fully deallocated to prevent resource leaks or crashes.

## Acceptance Criteria:

*   `SipManager` class is defined in `src/sip_server/sip_manager.py`.
*   `SipManager.start()` successfully initializes the PJSIP `Endpoint`, creates a UDP transport on the configured port, and starts the PJSIP library.
*   `SipManager.stop()` correctly shuts down and destroys the PJSIP library resources.
*   Basic PJSIP logging (if configured) shows library startup and transport creation.
*   No crashes occur during PJSIP initialization or shutdown.
