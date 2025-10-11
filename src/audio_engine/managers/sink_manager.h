/**
 * @file sink_manager.h
 * @brief Defines the SinkManager class for managing audio sinks (outputs).
 * @details This class is responsible for the creation, configuration, and lifecycle
 *          of `SinkAudioMixer` instances, which represent audio outputs.
 */
#ifndef SINK_MANAGER_H
#define SINK_MANAGER_H

#include "../configuration/audio_engine_config_types.h"
#include "../configuration/audio_engine_settings.h"
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

/**
 * @class SinkManager
 * @brief Manages all audio sinks (outputs) in the audio engine.
 * @details This class handles the lifecycle of `SinkAudioMixer` objects. It provides
 *          an interface to add and remove sinks, and to manage their connections
 *          to audio sources and listeners (like WebRTC peers).
 */
class SinkManager {
public:
    /**
     * @brief Constructs a SinkManager.
     * @param manager_mutex A reference to the main AudioManager mutex for thread safety.
     */
    SinkManager(std::recursive_mutex& manager_mutex, std::shared_ptr<screamrouter::audio::AudioEngineSettings> settings);
    /**
     * @brief Destructor.
     */
    ~SinkManager();

    /**
     * @brief Adds a new sink to the system.
     * @param config The configuration for the new sink.
     * @param running A flag indicating if the audio engine is running.
     * @return true if the sink was added successfully, false otherwise.
     */
    bool add_sink(const SinkConfig& config, bool running);
    /**
     * @brief Removes an existing sink from the system.
     * @param sink_id The unique ID of the sink to remove.
     * @return true if the sink was removed successfully, false otherwise.
     */
    bool remove_sink(const std::string& sink_id);

    /**
     * @brief Subscribes a sink to a source's output queue.
     * @param sink_id The ID of the sink.
     * @param source_instance_id The ID of the source instance.
     * @param queue The chunk queue from the source.
     */
    void add_input_queue_to_sink(const std::string& sink_id, const std::string& source_instance_id, std::shared_ptr<ChunkQueue> queue);
    /**
     * @brief Unsubscribes a sink from a source's output queue.
     * @param sink_id The ID of the sink.
     * @param source_instance_id The ID of the source instance.
     */
    void remove_input_queue_from_sink(const std::string& sink_id, const std::string& source_instance_id);
    /**
     * @brief Adds a network listener (e.g., a WebRTC peer) to a sink.
     * @param sink_id The ID of the sink.
     * @param listener_id A unique ID for the listener.
     * @param sender A pointer to the network sender for the listener.
     */
    void add_listener_to_sink(const std::string& sink_id, const std::string& listener_id, std::unique_ptr<INetworkSender> sender);
    /**
     * @brief Removes a network listener from a sink.
     * @param sink_id The ID of the sink.
     * @param listener_id The ID of the listener to remove.
     */
    void remove_listener_from_sink(const std::string& sink_id, const std::string& listener_id);
    /**
     * @brief Retrieves a network listener from a sink.
     * @param sink_id The ID of the sink.
     * @param listener_id The ID of the listener to retrieve.
     * @return A pointer to the network sender, or nullptr if not found.
     */
    INetworkSender* get_listener_from_sink(const std::string& sink_id, const std::string& listener_id);

    /** @brief Gets a reference to the map of sink configurations. */
    std::map<std::string, SinkConfig>& get_sink_configs();
    /** @brief Gets a reference to the map of MP3 output queues. */
    std::map<std::string, std::shared_ptr<Mp3Queue>>& get_mp3_output_queues();
    /** @brief Gets a list of all active sink IDs. */
    std::vector<std::string> get_sink_ids();

    /**
     * @brief Gets a vector of pointers to all active sink mixers.
     * @return A vector of `SinkAudioMixer` pointers.
     */
    std::vector<SinkAudioMixer*> get_all_mixers();

private:
    std::recursive_mutex& m_manager_mutex;
    std::shared_ptr<screamrouter::audio::AudioEngineSettings> m_settings;

    std::map<std::string, std::unique_ptr<SinkAudioMixer>> m_sinks;
    std::map<std::string, SinkConfig> m_sink_configs;
    std::map<std::string, std::shared_ptr<Mp3Queue>> m_mp3_output_queues;
};

} // namespace audio
} // namespace screamrouter

#endif // SINK_MANAGER_H