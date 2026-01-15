from fastapi import FastAPI, HTTPException
from screamrouter.configuration.configuration_manager import ConfigurationManager
from screamrouter_audio_engine import AudioEngineSettings, TimeshiftTuning, MixerTuning, SourceProcessorTuning, ProcessorTuning, SynchronizationSettings, SynchronizationTuning, RtpReceiverTuning

def buffer_to_dict(buf):
    return {
        "size": getattr(buf, "size", 0),
        "high_watermark": getattr(buf, "high_watermark", 0),
        "depth_ms": getattr(buf, "depth_ms", 0.0),
        "fill_percent": getattr(buf, "fill_percent", 0.0),
        "push_rate_per_second": getattr(buf, "push_rate_per_second", 0.0),
        "pop_rate_per_second": getattr(buf, "pop_rate_per_second", 0.0),
    }

def stats_to_dict(stats):
    """Converts the AudioEngineStats pybind11 object to a dictionary."""
    
    global_stats_dict = {
        "timeshift_buffer_total_size": stats.global_stats.timeshift_buffer_total_size,
        "packets_added_to_timeshift_per_second": stats.global_stats.packets_added_to_timeshift_per_second,
        "timeshift_inbound_buffer": buffer_to_dict(getattr(stats.global_stats, "timeshift_inbound_buffer", None)),
    }

    stream_stats_dict = {}
    if hasattr(stats, 'stream_stats'):
        for key, stream_stat in stats.stream_stats.items():
            stream_stats_dict[key] = {
                "jitter_estimate_ms": stream_stat.jitter_estimate_ms,
                "packets_per_second": stream_stat.packets_per_second,
                "timeshift_buffer_size": stream_stat.timeshift_buffer_size,
                "timeshift_buffer_late_packets": stream_stat.timeshift_buffer_late_packets,
                "timeshift_buffer_lagging_events": stream_stat.timeshift_buffer_lagging_events,
                "tm_buffer_underruns": stream_stat.tm_buffer_underruns,
                "tm_packets_discarded": stream_stat.tm_packets_discarded,
                "last_arrival_time_error_ms": stream_stat.last_arrival_time_error_ms,
                "total_anchor_adjustment_ms": stream_stat.total_anchor_adjustment_ms,
                "total_packets_in_stream": stream_stat.total_packets_in_stream,
                "target_buffer_level_ms": stream_stat.target_buffer_level_ms,
                "buffer_target_fill_percentage": stream_stat.buffer_target_fill_percentage,
                "playback_rate": getattr(stream_stat, "playback_rate", 1.0),
                "system_jitter_ms": getattr(stream_stat, "system_jitter_ms", 0.0),
                "last_system_delay_ms": getattr(stream_stat, "last_system_delay_ms", 0.0),
                "timeshift_buffer": buffer_to_dict(getattr(stream_stat, "timeshift_buffer", None)),
            }

    source_stats_list = []
    if hasattr(stats, 'source_stats'):
        for source_stat in stats.source_stats:
            source_stats_list.append({
                "instance_id": source_stat.instance_id,
                "source_tag": source_stat.source_tag,
                "input_queue_size": source_stat.input_queue_size,
                "output_queue_size": source_stat.output_queue_size,
                "packets_processed_per_second": source_stat.packets_processed_per_second,
                "reconfigurations": source_stat.reconfigurations,
                "playback_rate": getattr(source_stat, "playback_rate", 1.0),
                "input_samplerate": getattr(source_stat, "input_samplerate", 0.0),
                "output_samplerate": getattr(source_stat, "output_samplerate", 0.0),
                "resample_ratio": getattr(source_stat, "resample_ratio", 0.0),
                "input_buffer": buffer_to_dict(getattr(source_stat, "input_buffer", None)),
                "output_buffer": buffer_to_dict(getattr(source_stat, "output_buffer", None)),
                "process_buffer": buffer_to_dict(getattr(source_stat, "process_buffer", None)),
                "timeshift_buffer": buffer_to_dict(getattr(source_stat, "timeshift_buffer", None)),
                "last_packet_age_ms": getattr(source_stat, "last_packet_age_ms", 0.0),
                "last_origin_age_ms": getattr(source_stat, "last_origin_age_ms", 0.0),
                "chunks_pushed": getattr(source_stat, "chunks_pushed", 0),
                "discarded_packets": getattr(source_stat, "discarded_packets", 0),
                "avg_processing_ms": getattr(source_stat, "avg_processing_ms", 0.0),
                "peak_process_buffer_samples": getattr(source_stat, "peak_process_buffer_samples", 0),
            })

    def sink_input_to_dict(lane):
        return {
            "instance_id": lane.instance_id,
            "source_output_queue": buffer_to_dict(getattr(lane, "source_output_queue", None)),
            "ready_queue": buffer_to_dict(getattr(lane, "ready_queue", None)),
            "last_chunk_dwell_ms": getattr(lane, "last_chunk_dwell_ms", 0.0),
            "avg_chunk_dwell_ms": getattr(lane, "avg_chunk_dwell_ms", 0.0),
            "underrun_events": getattr(lane, "underrun_events", 0),
            "ready_total_received": getattr(lane, "ready_total_received", 0),
            "ready_total_popped": getattr(lane, "ready_total_popped", 0),
            "ready_total_dropped": getattr(lane, "ready_total_dropped", 0),
        }

    sink_stats_list = []
    if hasattr(stats, 'sink_stats'):
        for sink_stat in stats.sink_stats:
            webrtc_listeners_list = []
            if hasattr(sink_stat, 'webrtc_listeners'):
                for listener_stat in sink_stat.webrtc_listeners:
                    webrtc_listeners_list.append({
                        "listener_id": listener_stat.listener_id,
                        "connection_state": listener_stat.connection_state,
                        "pcm_buffer_size": listener_stat.pcm_buffer_size,
                        "packets_sent_per_second": listener_stat.packets_sent_per_second
                    })
            
            sink_stats_list.append({
                "sink_id": sink_stat.sink_id,
                "active_input_streams": sink_stat.active_input_streams,
                "total_input_streams": sink_stat.total_input_streams,
                "packets_mixed_per_second": sink_stat.packets_mixed_per_second,
                "sink_buffer_underruns": sink_stat.sink_buffer_underruns,
                "sink_buffer_overflows": sink_stat.sink_buffer_overflows,
                "mp3_buffer_overflows": sink_stat.mp3_buffer_overflows,
                "payload_buffer": buffer_to_dict(getattr(sink_stat, "payload_buffer", None)),
                "mp3_output_buffer": buffer_to_dict(getattr(sink_stat, "mp3_output_buffer", None)),
                "mp3_pcm_buffer": buffer_to_dict(getattr(sink_stat, "mp3_pcm_buffer", None)),
                "last_chunk_dwell_ms": getattr(sink_stat, "last_chunk_dwell_ms", 0.0),
                "avg_chunk_dwell_ms": getattr(sink_stat, "avg_chunk_dwell_ms", 0.0),
                "last_send_gap_ms": getattr(sink_stat, "last_send_gap_ms", 0.0),
                "avg_send_gap_ms": getattr(sink_stat, "avg_send_gap_ms", 0.0),
                "inputs": [sink_input_to_dict(l) for l in getattr(sink_stat, "inputs", [])],
                "webrtc_listeners": webrtc_listeners_list
            })

    return {
        "global_stats": global_stats_dict,
        "stream_stats": stream_stats_dict,
        "source_stats": source_stats_list,
        "sink_stats": sink_stats_list
    }

def settings_to_dict(settings: AudioEngineSettings):
    """Converts the AudioEngineSettings pybind11 object to a dictionary."""
    return {
        "timeshift_tuning": {
            "cleanup_interval_ms": settings.timeshift_tuning.cleanup_interval_ms,
            "late_packet_threshold_ms": settings.timeshift_tuning.late_packet_threshold_ms,
            "target_buffer_level_ms": settings.timeshift_tuning.target_buffer_level_ms,
            "loop_max_sleep_ms": settings.timeshift_tuning.loop_max_sleep_ms,
            "max_catchup_lag_ms": settings.timeshift_tuning.max_catchup_lag_ms,
            "max_clock_pending_packets": settings.timeshift_tuning.max_clock_pending_packets,
            "rtp_continuity_slack_seconds": settings.timeshift_tuning.rtp_continuity_slack_seconds,
            "rtp_session_reset_threshold_seconds": settings.timeshift_tuning.rtp_session_reset_threshold_seconds,
        },
        "mixer_tuning": {
            "mp3_bitrate_kbps": settings.mixer_tuning.mp3_bitrate_kbps,
            "mp3_vbr_enabled": settings.mixer_tuning.mp3_vbr_enabled,
            "mp3_output_queue_max_size": settings.mixer_tuning.mp3_output_queue_max_size,
            "underrun_hold_timeout_ms": settings.mixer_tuning.underrun_hold_timeout_ms,
            "max_input_queue_chunks": settings.mixer_tuning.max_input_queue_chunks,
            "min_input_queue_chunks": settings.mixer_tuning.min_input_queue_chunks,
            "max_ready_chunks_per_source": settings.mixer_tuning.max_ready_chunks_per_source,
            "max_queued_chunks": settings.mixer_tuning.max_queued_chunks,
        },
        "source_processor_tuning": {
            "command_loop_sleep_ms": settings.source_processor_tuning.command_loop_sleep_ms,
            "discontinuity_threshold_ms": settings.source_processor_tuning.discontinuity_threshold_ms,
        },
        "processor_tuning": {
            "oversampling_factor": settings.processor_tuning.oversampling_factor,
            "volume_smoothing_factor": settings.processor_tuning.volume_smoothing_factor,
            "dc_filter_cutoff_hz": settings.processor_tuning.dc_filter_cutoff_hz,
            "normalization_target_rms": settings.processor_tuning.normalization_target_rms,
            "normalization_attack_smoothing": settings.processor_tuning.normalization_attack_smoothing,
            "normalization_decay_smoothing": settings.processor_tuning.normalization_decay_smoothing,
            "dither_noise_shaping_factor": settings.processor_tuning.dither_noise_shaping_factor,
        },
        "synchronization": {
            "enable_multi_sink_sync": settings.synchronization.enable_multi_sink_sync,
        },
        "synchronization_tuning": {
            "barrier_timeout_ms": settings.synchronization_tuning.barrier_timeout_ms,
            "sync_proportional_gain": settings.synchronization_tuning.sync_proportional_gain,
            "max_rate_adjustment": settings.synchronization_tuning.max_rate_adjustment,
            "sync_smoothing_factor": settings.synchronization_tuning.sync_smoothing_factor,
        },
        "rtp_receiver_tuning": {
            "format_probe_duration_ms": settings.rtp_receiver_tuning.format_probe_duration_ms,
            "format_probe_min_bytes": settings.rtp_receiver_tuning.format_probe_min_bytes,
        },
        "system_audio_tuning": {
            "alsa_target_latency_ms": settings.system_audio_tuning.alsa_target_latency_ms,
            "alsa_periods_per_buffer": settings.system_audio_tuning.alsa_periods_per_buffer,
            "alsa_dynamic_latency_enabled": settings.system_audio_tuning.alsa_dynamic_latency_enabled,
            "alsa_latency_min_ms": settings.system_audio_tuning.alsa_latency_min_ms,
            "alsa_latency_max_ms": settings.system_audio_tuning.alsa_latency_max_ms,
            "alsa_latency_low_water_ms": settings.system_audio_tuning.alsa_latency_low_water_ms,
            "alsa_latency_high_water_ms": settings.system_audio_tuning.alsa_latency_high_water_ms,
            "alsa_latency_integral_gain": settings.system_audio_tuning.alsa_latency_integral_gain,
            "alsa_latency_rate_limit_ms_per_sec": settings.system_audio_tuning.alsa_latency_rate_limit_ms_per_sec,
            "alsa_latency_idle_decay_ms_per_sec": settings.system_audio_tuning.alsa_latency_idle_decay_ms_per_sec,
            "alsa_latency_apply_hysteresis_ms": settings.system_audio_tuning.alsa_latency_apply_hysteresis_ms,
            "alsa_latency_reconfig_cooldown_ms": settings.system_audio_tuning.alsa_latency_reconfig_cooldown_ms,
            "alsa_latency_xrun_boost_ms": settings.system_audio_tuning.alsa_latency_xrun_boost_ms,
            "alsa_latency_low_step_ms": settings.system_audio_tuning.alsa_latency_low_step_ms,
        }
    }

def dict_to_settings(settings_dict: dict, existing_settings: AudioEngineSettings):
    """Updates an AudioEngineSettings object from a dictionary."""

    def apply_updates(section, updates):
        for key, value in updates.items():
            if hasattr(section, key):
                setattr(section, key, value)

    apply_updates(existing_settings.timeshift_tuning, settings_dict.get("timeshift_tuning", {}))
    apply_updates(existing_settings.mixer_tuning, settings_dict.get("mixer_tuning", {}))
    apply_updates(existing_settings.source_processor_tuning, settings_dict.get("source_processor_tuning", {}))
    apply_updates(existing_settings.processor_tuning, settings_dict.get("processor_tuning", {}))
    apply_updates(existing_settings.synchronization, settings_dict.get("synchronization", {}))
    apply_updates(existing_settings.synchronization_tuning, settings_dict.get("synchronization_tuning", {}))
    apply_updates(existing_settings.rtp_receiver_tuning, settings_dict.get("rtp_receiver_tuning", {}))
    apply_updates(existing_settings.system_audio_tuning, settings_dict.get("system_audio_tuning", {}))
    return existing_settings

class APIStats:
    """Holds the API endpoints for retrieving audio engine stats and settings"""
    def __init__(self, main_api: FastAPI, config_manager: ConfigurationManager):
        self.main_api = main_api
        self.config_manager = config_manager
        self.main_api.add_api_route("/api/stats", self.get_stats, methods=["GET"])
        self.main_api.add_api_route("/api/settings", self.get_settings, methods=["GET"])
        self.main_api.add_api_route("/api/settings", self.set_settings, methods=["POST"])

    async def get_stats(self):
        """Get audio engine stats"""
        if not self.config_manager.cpp_audio_manager:
            raise HTTPException(status_code=503, detail="C++ audio manager not available")
        
        stats = self.config_manager.cpp_audio_manager.get_audio_engine_stats()
        return stats_to_dict(stats)

    async def get_settings(self):
        """Get audio engine settings"""
        if not self.config_manager.cpp_audio_manager:
            raise HTTPException(status_code=503, detail="C++ audio manager not available")
        
        settings = self.config_manager.cpp_audio_manager.get_audio_settings()
        return settings_to_dict(settings)

    async def set_settings(self, settings_data: dict):
        """Set audio engine settings"""
        if not self.config_manager.cpp_audio_manager:
            raise HTTPException(status_code=503, detail="C++ audio manager not available")
        
        current_settings = self.config_manager.cpp_audio_manager.get_audio_settings()
        updated_settings = dict_to_settings(settings_data, current_settings)
        self.config_manager.cpp_audio_manager.set_audio_settings(updated_settings)
        return {"message": "Settings updated successfully"}
