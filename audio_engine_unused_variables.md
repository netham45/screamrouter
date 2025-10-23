# Unused Variables Identified in `src/audio_engine`

The following variables are declared within the audio engine sources but are not referenced anywhere else in the C++ codebase. Paths are relative to the repository root.

| Variable | Location | Notes |
| --- | --- | --- |
| `timeshift_buffer_duration_sec` | `src/audio_engine/audio_types.h:462` | Member of `SourceProcessorConfig`; never read after initialization. |
| `m_last_known_playback_rate` | `src/audio_engine/audio_processor/audio_processor.h:138` | Private member of `AudioProcessor`; unused alongside `playback_rate_`. |
| `resample_float_in_buffer_` | `src/audio_engine/audio_processor/audio_processor.h:157` | Buffer reserved in `AudioProcessor`; never used. |
| `RTP_CLOCK_RATE_L16_48K_STEREO` | `src/audio_engine/receivers/rtp/rtp_receiver.cpp:54` | Constant defined next to other RTP metadata but not referenced. |
| `RTP_CHANNELS_L16_48K_STEREO` | `src/audio_engine/receivers/rtp/rtp_receiver.cpp:55` | Constant defined but unused. |
| `RTP_BITS_PER_SAMPLE_L16_48K_STEREO` | `src/audio_engine/receivers/rtp/rtp_receiver.cpp:56` | Constant defined but unused. |
| `UNICAST_ADDR` | `src/audio_engine/receivers/rtp/sap_listener.cpp:25` | Placeholder address constant; not referenced. |
| `SessionInfo::destination_ip` | `src/audio_engine/receivers/rtp/rtp_receiver.h:180` | Field of the nested `SessionInfo` struct that is never set or read. |
| `chunk_conversion_buffer_` | `src/audio_engine/receivers/system/alsa_capture_receiver.h:85` | Declared conversion buffer in ALSA capture receiver; never accessed. |
| `SCREAM_HEADER_SIZE` | `src/audio_engine/input_processor/source_input_processor.h:49` | Constant for scream header length; unused within input processor. |
| `DEFAULT_INPUT_BITDEPTH` | `src/audio_engine/input_processor/source_input_processor.h:53` | Default assumption constant; unused. |
| `DEFAULT_INPUT_CHANNELS` | `src/audio_engine/input_processor/source_input_processor.h:55` | Default assumption constant; unused. |
| `DEFAULT_INPUT_SAMPLERATE` | `src/audio_engine/input_processor/source_input_processor.h:57` | Default assumption constant; unused. |
| `total_barrier_wait` | `src/audio_engine/synchronization/global_synchronization_clock.cpp:322` | Local accumulator declared in `get_stats`; never used. |
| `wait_count` | `src/audio_engine/synchronization/global_synchronization_clock.cpp:323` | Local counter declared in `get_stats`; never used. |
| `DEFAULT_OPUS_AUDIO_PROFILE` | `src/audio_engine/senders/webrtc/webrtc_sender.h:112` | Private constant of `WebRtcSender`; no references. |
| `source_sample_format_` | `src/audio_engine/senders/system/wasapi_playback_sender.h:67` | Tracks source sample format in WASAPI sender; never set or read. |

*Scope:* search excluded vendored dependencies under `src/audio_engine/deps` and build artifacts.
