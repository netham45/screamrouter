# API WebRTC Callback Flow (`screamrouter/api/api_webrtc.py`)

This file is the Python side of the WebRTC (WHEP) listener bridge into the C++ audio engine. It wires HTTP endpoints to the C++ `AudioManager.add_webrtc_listener` call, which invokes Python callbacks from C++ threads.

## Callback contracts
- `on_local_description(sdp_str: str)`  
  - Called by the C++ engine once the answer SDP is ready.  
  - Runs on a C++ thread; only uses thread-safe operations (sets an `asyncio.Event`, stores string).
  - Failure to set the event results in a 5s timeout and a 500 to the client.

- `on_ice_candidate(candidate: str, sdp_mid: str)`  
  - Called by the C++ engine whenever it gathers a new ICE candidate.  
  - Also runs on a C++ thread; appends to `pending_server_candidates[listener_id]` so the client can poll `/api/whep/{sink_id}/{listener_id}/candidates`.

Both callbacks close over `listener_id`, `answer_sdp`, and shared dicts. They never call FastAPI directly; they only mutate in‑memory state that is later read by HTTP handlers.

## Threading and safety
- C++ invokes both callbacks on its own threads. Keep them minimal and non-blocking. The current implementation only logs and updates Python dicts.
- `listeners_lock` is an `asyncio.Lock` used for listener bookkeeping. The callbacks do **not** take this lock; they write to dicts that are only mutated from the event loop in other coroutines. If you add heavier logic in callbacks, guard shared state.
- Timeouts: WHEP POST waits up to 5s for `on_local_description` to fire; if it never does, the session is aborted and cleaned up.

## HTTP endpoints vs callback data
- `/api/whep/{sink_id}` (POST): creates listener, registers callbacks, waits for SDP answer from `on_local_description`, then returns 201 with SDP answer and Location header.
- `/api/whep/{sink_id}/{listener_id}/candidates` (GET): client polls; returns and clears the server ICE candidates collected by `on_ice_candidate`.
- `/api/whep/{sink_id}/{listener_id}` (PATCH): forwards client ICE candidates to `AudioManager.add_webrtc_remote_ice_candidate`.
- `/api/whep/{sink_id}/{listener_id}` (DELETE) and `/api/whep/{sink_id}/{listener_id}` (POST heartbeat) coordinate with background stale-listener cleanup.

## Temporary entities and cleanup
- When listening to a source/route, the API can create temporary sinks/routes (`setup_source_listener`, `setup_route_listener`). Those are tracked in `temporary_entities` keyed by `listener_id`.
- Stale listeners (>15s no heartbeat) trigger `_check_stale_listeners`, which calls `AudioManager.remove_webrtc_listener`, deletes pending candidates, and removes temporary sinks/routes.
- `whep_delete` also removes candidates and removes the C++ listener. Both paths must stay consistent with the callback-created state.

## Extending safely
- Keep callbacks idempotent and short; defer heavy work to the event loop (use `asyncio.get_event_loop().call_soon_threadsafe(...)` if you need to signal coroutines).
- If you add shared mutable structures touched by callbacks, protect them with thread-safe primitives or use `call_soon_threadsafe` to mutate on the loop.
- Avoid awaiting inside callbacks; they are not async and run on foreign threads.
- When changing the callback signatures in C++ bindings, update the call site here and adjust timeout/error handling accordingly.

## Example: adding metrics from callbacks
```python
from asyncio import get_event_loop

metrics = {"candidates": 0}

def on_ice_candidate(candidate: str, sdp_mid: str):
    # Schedule metric increment on the event loop to avoid threading issues
    loop = get_event_loop()
    loop.call_soon_threadsafe(lambda: metrics.__setitem__("candidates", metrics["candidates"] + 1))
    pending_server_candidates.setdefault(listener_id, []).append({"candidate": candidate, "sdpMid": sdp_mid})
```
