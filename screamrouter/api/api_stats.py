from fastapi import FastAPI, HTTPException
from screamrouter.configuration.configuration_manager import ConfigurationManager
from screamrouter_audio_engine import AudioEngineSettings, TimeshiftTuning, MixerTuning, SourceProcessorTuning, ProcessorTuning, SynchronizationSettings, SynchronizationTuning

def stats_to_dict(stats):
    """Converts the AudioEngineStats pybind11 object to a dictionary."""
    
    global_stats_dict = {
        "timeshift_buffer_total_size": stats.global_stats.timeshift_buffer_total_size,
        "packets_added_to_timeshift_per_second": stats.global_stats.packets_added_to_timeshift_per_second
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
                "buffer_target_fill_percentage": stream_stat.buffer_target_fill_percentage
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
                "reconfigurations": source_stat.reconfigurations
            })

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
            "reanchor_interval_sec": settings.timeshift_tuning.reanchor_interval_sec,
            "jitter_smoothing_factor": settings.timeshift_tuning.jitter_smoothing_factor,
            "jitter_safety_margin_multiplier": settings.timeshift_tuning.jitter_safety_margin_multiplier,
            "late_packet_threshold_ms": settings.timeshift_tuning.late_packet_threshold_ms,
            "target_buffer_level_ms": settings.timeshift_tuning.target_buffer_level_ms,
            "proportional_gain_kp": settings.timeshift_tuning.proportional_gain_kp,
            "min_playback_rate": settings.timeshift_tuning.min_playback_rate,
            "max_playback_rate": settings.timeshift_tuning.max_playback_rate,
            "loop_max_sleep_ms": settings.timeshift_tuning.loop_max_sleep_ms,
        },
        "mixer_tuning": {
            "grace_period_timeout_ms": settings.mixer_tuning.grace_period_timeout_ms,
            "grace_period_poll_interval_ms": settings.mixer_tuning.grace_period_poll_interval_ms,
            "mp3_bitrate_kbps": settings.mixer_tuning.mp3_bitrate_kbps,
            "mp3_vbr_enabled": settings.mixer_tuning.mp3_vbr_enabled,
            "mp3_output_queue_max_size": settings.mixer_tuning.mp3_output_queue_max_size,
        },
        "source_processor_tuning": {
            "command_loop_sleep_ms": settings.source_processor_tuning.command_loop_sleep_ms,
        },
        "processor_tuning": {
            "oversampling_factor": settings.processor_tuning.oversampling_factor,
            "volume_smoothing_factor": settings.processor_tuning.volume_smoothing_factor,
            "dc_filter_cutoff_hz": settings.processor_tuning.dc_filter_cutoff_hz,
            "soft_clip_threshold": settings.processor_tuning.soft_clip_threshold,
            "soft_clip_knee": settings.processor_tuning.soft_clip_knee,
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
        }
    }

def dict_to_settings(settings_dict: dict, existing_settings: AudioEngineSettings):
    """Updates an AudioEngineSettings object from a dictionary."""
    for key, value in settings_dict.get("timeshift_tuning", {}).items():
        setattr(existing_settings.timeshift_tuning, key, value)
    for key, value in settings_dict.get("mixer_tuning", {}).items():
        setattr(existing_settings.mixer_tuning, key, value)
    for key, value in settings_dict.get("source_processor_tuning", {}).items():
        setattr(existing_settings.source_processor_tuning, key, value)
    for key, value in settings_dict.get("processor_tuning", {}).items():
        setattr(existing_settings.processor_tuning, key, value)
    for key, value in settings_dict.get("synchronization", {}).items():
        setattr(existing_settings.synchronization, key, value)
    for key, value in settings_dict.get("synchronization_tuning", {}).items():
        setattr(existing_settings.synchronization_tuning, key, value)
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
