# Core Types & Stats (`src/audio_engine/audio_types.h`)

Defines most Python-visible structs/enums/constants used across the engine.

## Configuration carriers
- `SinkConfig`: id, output_ip/port, bitdepth, samplerate, channels, chlayout1/chlayout2, protocol, SAP hints, time sync flags, `rtp_receivers` (list of `RtpReceiverConfig`), `multi_device_mode`.
- `CppSpeakerLayout`: `auto_mode` flag plus 8x8 `matrix` (float gains). Defaults to auto + identity.
- `DeviceDirection`: `CAPTURE | PLAYBACK`.
- `DeviceCapabilityRange`: `min`, `max`.
- `SystemDeviceInfo`: tag, friendly_name, hw_id, endpoint_id, card/device indices, direction, channels range, sample_rates range, bit_depth, present flag.
- `DeviceDiscoveryNotification`: direction, present, tag.

## Stats structs
- `BufferMetrics`: size, high_watermark, depth_ms, fill_percent, push/pop rates.
- `StreamStats`: jitter and timing metrics, playback_rate, timeshift buffer counters, buffer/clock fields, `timeshift_buffer: BufferMetrics`.
- `SourceStats`: instance_id, source_tag, queue sizes, packets_processed_per_second, reconfigurations, playback/resample info, multiple `BufferMetrics` views, timing counters.
- `SinkStats`: sink_id, counts of active/total inputs, mixed packets/s, underruns/overflows, MP3 buffer stats, ready/PCM buffers, dwell/send gap metrics, input lanes list, WebRTC listeners list.
- `WebRtcListenerStats`: listener_id, connection_state, pcm_buffer_size, packets_sent_per_second.
- `GlobalStats`: `timeshift_buffer_total_size`, `packets_added_to_timeshift_per_second`, optional timeshift_inbound_buffer.
- `AudioEngineStats`: container of `global_stats`, `sink_stats[]`, `source_stats[]`, `stream_stats{}`.
- `TimeshiftBufferExport`: sample_rate, channels, bit_depth, chunk_size_bytes, duration_seconds, earliest/latest packet ages, requested lookback, `pcm_data` bytes.

## Other exported items
- `EQ_BANDS` constant is added in `bindings.cpp` (value 18).
- Enums also support `__int__`, `__repr__` via pybind defaults; use as Python enums.

## Threading/safety
- All structs are POD-like; pybind exposes fields directly (`def_readwrite`). No internal locking—safe to read/write from Python but coordinate with engine state externally.

## Typical flows
- Build `SinkConfig` for desired outputs; used inside `AppliedSinkParams` in config translation.
- Inspect stats snapshots: `AudioManager.get_audio_engine_stats()` returns `AudioEngineStats`; convert via direct attribute reads or helper (see `api/api_stats.py`).
- Use `CppSpeakerLayout` in `AppliedSourcePathParams.speaker_layouts_map` for per-channel mixing maps.
