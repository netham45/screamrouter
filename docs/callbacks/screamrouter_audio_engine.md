# C++ Engine Callback Entry Points (`screamrouter_audio_engine.pyi`)

This stub documents Python-visible functions that call into C++ and accept callbacks. All callbacks are invoked from C++ threads; keep them non-blocking and thread-safe.

## `AudioManager.add_webrtc_listener(...) -> bool`
- **Purpose:** Registers a WebRTC listener on a sink (RTP/PCM source → WebRTC).  
- **Signature:** `add_webrtc_listener(sink_id, listener_id, offer_sdp, on_local_description_cb: Callable[[str], None], on_ice_candidate_cb: Callable[[str, str], None], client_ip)`.  
- **Callback order:**
  1. Engine parses `offer_sdp`, creates transport.
  2. Emits `on_local_description_cb(answer_sdp_str)` once the local SDP is ready.
  3. Emits `on_ice_candidate_cb(candidate_str, sdp_mid)` for each ICE candidate as they are discovered.
- **Thread model:** Both callbacks are called on C++ worker threads. Avoid blocking; use `asyncio.get_running_loop().call_soon_threadsafe(...)` to hand work to the event loop if needed.
- **Return:** `True` if listener was registered. On failure, no callbacks fire.

## Related signaling helpers
- `add_webrtc_remote_ice_candidate(sink_id, listener_id, candidate, sdpMid)` forwards client ICE to C++; no callbacks.
- `remove_webrtc_listener(sink_id, listener_id)` detaches a listener and stops further callback invocations.

## Log forwarding (poll style)
- The C++ logger does **not** push callbacks; instead, Python polls:
  - `get_cpp_log_messages(timeout_ms=100)` blocks until messages or timeout; returns `[(LogLevel_CPP, message, filename, line)]`.
  - `shutdown_cpp_logger()` unblocks any polling before shutdown.
- Typical pattern in `screamrouter/screamrouter_logger/screamrouter_logger.py`: run a background thread that polls `get_cpp_log_messages` and forwards into Python logging.

## Safety recommendations
- Keep callback code reentrant and minimal; no awaits, no long CPU tasks.
- Never mutate shared state from callbacks without synchronization; prefer dispatching onto the main event loop.
- If you need per-listener context inside callbacks, close over immutable IDs only (e.g., `listener_id`); avoid closing over large mutable objects shared elsewhere.
