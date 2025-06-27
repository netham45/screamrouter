/**
 * @file mp3_data_api_manager.h
 * @brief Defines the MP3DataApiManager class for retrieving encoded MP3 data.
 * @details This class provides methods to access the MP3 output queues of sinks,
 *          allowing external components (like the Python API) to pull encoded audio data.
 */
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

/**
 * @class MP3DataApiManager
 * @brief Manages access to MP3 data queues from audio sinks.
 * @details This class provides a thread-safe API for retrieving chunks of
 *          MP3-encoded audio data from the output queues of specified sinks.
 *          It allows retrieval by sink ID or by the sink's output IP address.
 */
class MP3DataApiManager {
public:
    /**
     * @brief Constructs an MP3DataApiManager.
     * @param manager_mutex A reference to the main AudioManager mutex for thread safety.
     * @param mp3_output_queues A reference to the map of MP3 output queues, keyed by sink ID.
     * @param sink_configs A reference to the map of sink configurations.
     */
    MP3DataApiManager(
        std::mutex& manager_mutex,
        std::map<std::string, std::shared_ptr<Mp3Queue>>& mp3_output_queues,
        std::map<std::string, SinkConfig>& sink_configs
    );
    /**
     * @brief Destructor.
     */
    ~MP3DataApiManager();

    /**
     * @brief Retrieves a chunk of MP3 data from a specific sink.
     * @param sink_id The unique ID of the sink.
     * @param running A flag indicating if the audio engine is running.
     * @return A vector of bytes containing the MP3 data, or an empty vector if none is available.
     */
    std::vector<uint8_t> get_mp3_data(const std::string& sink_id, bool running);
    
    /**
     * @brief Retrieves a chunk of MP3 data from a sink identified by its output IP address.
     * @param ip_address The output IP address of the sink.
     * @param running A flag indicating if the audio engine is running.
     * @return A vector of bytes containing the MP3 data, or an empty vector if none is available.
     */
    std::vector<uint8_t> get_mp3_data_by_ip(const std::string& ip_address, bool running);

private:
    std::mutex& m_manager_mutex;
    std::map<std::string, std::shared_ptr<Mp3Queue>>& m_mp3_output_queues;
    std::map<std::string, SinkConfig>& m_sink_configs;
};

} // namespace audio
} // namespace screamrouter

#endif // DATA_API_MANAGER_H