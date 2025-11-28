# Module Assembly (`src/audio_engine/bindings.cpp`)

The module definition orchestrates all pybind11 bindings that make up `screamrouter_audio_engine`. It imports binding helpers and exposes them in dependency order.

- **Module name:** `screamrouter_audio_engine`
- **Binding order (matters for type dependencies):**
  1) `audio::logging::bind_logger` — C++ logger bridge (`LogLevel_CPP`, log polling functions)
  2) `audio::bind_audio_types` — Core structs/enums/stats/config types (see `audio_types.md`)
  3) `config::bind_config_types` — Desired state structs for configuration application (see `audio_engine_config_types.md`)
  4) `audio::bind_audio_manager` — Main engine API (methods on `AudioManager`; see `audio_manager.md`)
  5) `config::bind_config_applier` — `AudioEngineConfigApplier` to apply desired state (see `audio_engine_config_applier.md`)
- **Constants:** `EQ_BANDS` bound as a module attribute.
- **Desktop overlay:** On Windows, binds `DesktopOverlay`; elsewhere a stub is bound that raises when `start` is used.

Importing `screamrouter_audio_engine` runs this sequence; if you add new binding helpers ensure they’re inserted here respecting dependencies.
