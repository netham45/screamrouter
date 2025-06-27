from fastapi import FastAPI, HTTPException
from src.configuration.configuration_manager import ConfigurationManager

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
                "timeshift_buffer_size": stream_stat.timeshift_buffer_size
            }

    source_stats_list = []
    if hasattr(stats, 'source_stats'):
        for source_stat in stats.source_stats:
            source_stats_list.append({
                "instance_id": source_stat.instance_id,
                "source_tag": source_stat.source_tag,
                "input_queue_size": source_stat.input_queue_size,
                "output_queue_size": source_stat.output_queue_size,
                "packets_processed_per_second": source_stat.packets_processed_per_second
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
                "webrtc_listeners": webrtc_listeners_list
            })

    return {
        "global_stats": global_stats_dict,
        "stream_stats": stream_stats_dict,
        "source_stats": source_stats_list,
        "sink_stats": sink_stats_list
    }

class APIStats:
    """Holds the API endpoints for retrieving audio engine stats"""
    def __init__(self, main_api: FastAPI, config_manager: ConfigurationManager):
        self.main_api = main_api
        self.config_manager = config_manager
        self.main_api.add_api_route("/api/stats", self.get_stats, methods=["GET"])

    async def get_stats(self):
        """Get audio engine stats"""
        if not self.config_manager.cpp_audio_manager:
            raise HTTPException(status_code=503, detail="C++ audio manager not available")
        
        stats = self.config_manager.cpp_audio_manager.get_audio_engine_stats()
        return stats_to_dict(stats)