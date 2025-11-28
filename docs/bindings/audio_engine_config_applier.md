# Config Applier (`src/audio_engine/configuration/audio_engine_config_applier.h`)

Bridges Python desired state to the live engine. Holds shadow state of active sinks/paths and reconciles differences.

## Bound class
- `AudioEngineConfigApplier(audio_manager: AudioManager)`  
  Constructor stores a reference; one applier should manage a given manager.
- `apply_state(desired_state: DesiredEngineState) -> bool`  
  Applies the provided desired state. The binding releases the GIL while C++ mutates engine state (`py::gil_scoped_release`).

## Behavior (C++)
- Keeps cached desired state and shadow maps of sinks/paths to compute adds/updates/removals.
- Resolves wildcard/per-process tags as concrete streams appear; can reapply cached state when tags materialize (`PendingStream` handling).
- Reconciles connections per sink after sinks/paths are reconciled.

## Usage pattern
```python
desired = ae.DesiredEngineState()
desired.sinks = [...]
desired.source_paths = [...]
applier = ae.AudioEngineConfigApplier(audio_manager)
ok = applier.apply_state(desired)
```
- Call after constructing `DesiredEngineState` via Python models or translator.
- Because the GIL is released during apply, avoid sharing mutable Python objects into C++ beyond the bound structs themselves.

## Safety/locking
- Internally uses a recursive mutex around apply. From Python, serialize calls to `apply_state` to avoid redundant work.
- Exceptions inside apply propagate as Python exceptions; check return or catch to handle partial state.
