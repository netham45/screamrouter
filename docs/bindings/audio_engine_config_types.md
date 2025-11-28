# Desired-State Types (`src/audio_engine/configuration/audio_engine_config_types.h`)

These bindings define the data structures passed from Python to C++ when applying a full engine configuration.

## `RtpReceiverConfig`
- Fields: `receiver_id`, `ip_address`, `port`, `channel_map` (tuple of two uint8), `enabled`.
- Used for multi-device RTP fan-out on a sink.

## `AppliedSourcePathParams`
- Identity/routing: `path_id`, `source_tag`, `target_sink_id`, `generated_instance_id` (set by C++).
- Audio controls: `volume`, `eq_values` (length = `EQ_BANDS`), `eq_normalization`, `volume_normalization`, `delay_ms`, `timeshift_sec`.
- Format targets: `target_output_channels`, `target_output_samplerate`.
- Capture hints: `source_input_channels`, `source_input_samplerate`, `source_input_bitdepth`.
- Speaker mapping: `speaker_layouts_map {input_channels -> CppSpeakerLayout}`.
- Default ctor builds a flat EQ list sized to `EQ_BANDS`.

## `AppliedSinkParams`
- `sink_id`
- `sink_engine_config` (`SinkConfig` from `audio_types.h`)
- `connected_source_path_ids[]` — the `path_id`s routed into this sink.

## `DesiredEngineState`
- `source_paths[]: AppliedSourcePathParams`
- `sinks[]: AppliedSinkParams`
- This is the top-level object consumed by `AudioEngineConfigApplier.apply_state`.

## Notes for Python callers
- `channel_map` is exposed as a Python tuple `(left, right)`; assignment validates size 2.
- No internal validation of EQ list length beyond what Python gives; ensure length matches `screamrouter_audio_engine.EQ_BANDS`.
- `generated_instance_id` is written by C++; treat as read-only from Python.
