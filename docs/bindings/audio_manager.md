# Audio Manager & Tuning (`src/audio_engine/managers/audio_manager.h`)

Primary engine API exposed to Python, plus tuning structs.

## Tuning classes
- `TimeshiftTuning`: cleanup_interval_ms, late_packet_threshold_ms, target_buffer_level_ms, loop_max_sleep_ms, max_catchup_lag_ms, max_clock_pending_packets, rtp_continuity_slack_seconds, rtp_session_reset_threshold_seconds, playback_ratio_* (limits, slew, PI gains, smoothing), playback_catchup_*.
- `ProfilerSettings`: enabled, log_interval_ms.
- `MixerTuning`: mp3_bitrate_kbps, mp3_vbr_enabled, mp3_output_queue_max_size, underrun_hold_timeout_ms, max/min input queue chunks & duration, max_ready_chunks_per_source, max_ready_queue_duration_ms.
- `SourceProcessorTuning`: command_loop_sleep_ms, discontinuity_threshold_ms.
- `ProcessorTuning`: oversampling_factor, volume_smoothing_factor, dc_filter_cutoff_hz, normalization_{target_rms,attack_smoothing,decay_smoothing}, dither_noise_shaping_factor.
- `SynchronizationSettings`: enable_multi_sink_sync.
- `SynchronizationTuning`: barrier_timeout_ms, sync_proportional_gain, max_rate_adjustment, sync_smoothing_factor.
- `AudioEngineSettings`: chunk_size_bytes, base_frames_per_chunk_mono16, and aggregates all tunings above.
- `TimeshiftBufferExport`: read-only fields `sample_rate`, `channels`, `bit_depth`, `chunk_size_bytes`, `duration_seconds`, `earliest_packet_age_seconds`, `latest_packet_age_seconds`, `lookback_seconds_requested`, `pcm_data` bytes.

## `AudioManager` methods (bound)
- Lifecycle: `initialize(rtp_listen_port=40000, global_timeshift_buffer_duration_sec=300) -> bool`; `shutdown()`.
- Tuning: `get_audio_settings() -> AudioEngineSettings`; `set_audio_settings(settings)`.
- Stats: `get_audio_engine_stats() -> AudioEngineStats` (types in `audio_types.md`).
- Timeshift: `export_timeshift_buffer(source_tag, lookback_seconds=300.0) -> TimeshiftBufferExport | None`.
- MP3: `get_mp3_data_by_ip(ip_address) -> bytes`; chunk sizing via `get_chunk_size_bytes_for_format(channels, bit_depth)`.
- Discovery: `get_rtp_receiver_seen_tags()`, `get_raw_scream_receiver_seen_tags(listen_port)`, `get_per_process_scream_receiver_seen_tags(listen_port)`, `get_pulse_receiver_seen_tags()` (non-Windows), `get_rtp_sap_announcements()`.
- System devices: `list_system_devices() -> dict[tag, SystemDeviceInfo]`; `drain_device_notifications() -> list[DeviceDiscoveryNotification]`.
- Plugins: `write_plugin_packet(source_instance_id, audio_payload: bytes, channels, sample_rate, bit_depth, chlayout1, chlayout2) -> bool` (copies bytes into a vector then injects).
- WebRTC:
  - `add_webrtc_listener(sink_id, listener_id, offer_sdp, on_local_description_cb, on_ice_candidate_cb, client_ip) -> bool`
  - `remove_webrtc_listener(sink_id, listener_id) -> bool`
  - `add_webrtc_remote_ice_candidate(sink_id, listener_id, candidate, sdpMid) -> None`
  - Callbacks are invoked from C++ threads; keep handlers non-blocking.

## Notes
- `write_plugin_packet` converts Python `bytes` to a vector; ensure payload aligns with `get_chunk_size_bytes_for_format`.
- `add_webrtc_listener` expects callable objects; see `api_webrtc.md` for threading behavior.
- Tuning structs are plain data; you can mutate attributes then `set_audio_settings`.
