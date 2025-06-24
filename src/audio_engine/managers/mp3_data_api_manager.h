#ifndef DATA_API_MANAGER_H
#define DATA_API_MANAGER_H

#include "../audio_types.h"
#include "../configuration/audio_engine_config_types.h"
#include "../utils/thread_safe_queue.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>


namespace screamrouter {
namespace audio {
using Mp3Queue = utils::ThreadSafeQueue<EncodedMP3Data>;
using ChunkQueue = utils::ThreadSafeQueue<ProcessedAudioChunk>;


class MP3DataApiManager {
public:
    MP3DataApiManager(
        std::mutex& manager_mutex,
        std::map<std::string, std::shared_ptr<Mp3Queue>>& mp3_output_queues,
        std::map<std::string, SinkConfig>& sink_configs
    );
    ~MP3DataApiManager();

    std::vector<uint8_t> get_mp3_data(const std::string& sink_id, bool running);
    std::vector<uint8_t> get_mp3_data_by_ip(const std::string& ip_address, bool running);

private:
    std::mutex& m_manager_mutex;
    std::map<std::string, std::shared_ptr<Mp3Queue>>& m_mp3_output_queues;
    std::map<std::string, SinkConfig>& m_sink_configs;
};

} // namespace audio
} // namespace screamrouter

#endif // DATA_API_MANAGER_H