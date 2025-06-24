#ifndef SINK_MANAGER_H
#define SINK_MANAGER_H

#include "../configuration/audio_engine_config_types.h"
#include "../output_mixer/sink_audio_mixer.h"
#include "../utils/thread_safe_queue.h"
#include "../audio_types.h"
#include <string>
#include <memory>
#include <map>
#include <mutex>

namespace screamrouter {
namespace audio {
using ChunkQueue = utils::ThreadSafeQueue<ProcessedAudioChunk>;
using Mp3Queue = utils::ThreadSafeQueue<EncodedMP3Data>;

class SinkManager {
public:
    SinkManager(std::mutex& manager_mutex);
    ~SinkManager();

    bool add_sink(const SinkConfig& config, bool running);
    bool remove_sink(const std::string& sink_id);

    // Forwarding methods for WebRTC and Connection Manager
    void add_input_queue_to_sink(const std::string& sink_id, const std::string& source_instance_id, std::shared_ptr<ChunkQueue> queue);
    void remove_input_queue_from_sink(const std::string& sink_id, const std::string& source_instance_id);
    void add_listener_to_sink(const std::string& sink_id, const std::string& listener_id, std::unique_ptr<INetworkSender> sender);
    void remove_listener_from_sink(const std::string& sink_id, const std::string& listener_id);
    INetworkSender* get_listener_from_sink(const std::string& sink_id, const std::string& listener_id);

    // Getter methods for AudioManager
    std::map<std::string, SinkConfig>& get_sink_configs();
    std::map<std::string, std::shared_ptr<Mp3Queue>>& get_mp3_output_queues();
    std::vector<std::string> get_sink_ids();

private:
    std::mutex& m_manager_mutex;

    std::map<std::string, std::unique_ptr<SinkAudioMixer>> m_sinks;
    std::map<std::string, SinkConfig> m_sink_configs;
    std::map<std::string, std::shared_ptr<Mp3Queue>> m_mp3_output_queues;
};

} // namespace audio
} // namespace screamrouter

#endif // SINK_MANAGER_H