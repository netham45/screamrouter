/**
 * @file source_manager.h
 * @brief Defines the SourceManager class for managing audio sources.
 * @details This class handles the creation, configuration, and lifecycle of
 *          `SourceInputProcessor` instances, which represent individual audio sources.
 */
#ifndef SOURCE_MANAGER_H
#define SOURCE_MANAGER_H

#include "../audio_types.h"
#include "../configuration/audio_engine_config_types.h"
#include "../configuration/audio_engine_settings.h"
#include "../input_processor/source_input_processor.h"
#include "../utils/thread_safe_queue.h"
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <mutex>

namespace screamrouter {
namespace audio {

class TimeshiftManager;

/**
 * @class SourceManager
 * @brief Manages all audio sources in the audio engine.
 * @details This class is responsible for the lifecycle of `SourceInputProcessor` objects.
 *          It provides an interface to configure new sources and remove existing ones,
 *          and it manages the various queues associated with each source.
 */
class SourceManager {
public:
    /**
     * @brief Constructs a SourceManager.
     * @param manager_mutex A reference to the main AudioManager mutex for thread safety.
     * @param timeshift_manager A pointer to the TimeshiftManager for registering new processors.
     * @param settings Shared pointer to audio engine settings.
     */
    SourceManager(std::recursive_mutex& manager_mutex, TimeshiftManager* timeshift_manager, std::shared_ptr<screamrouter::audio::AudioEngineSettings> settings);
    
    /**
     * @brief Sets callbacks for managing system audio capture devices.
     * @param ensure_callback Callback to activate a system audio capture device.
     * @param release_callback Callback to release a system audio capture device.
     */
    void set_capture_device_callbacks(
        std::function<bool(const std::string&)> ensure_callback,
        std::function<void(const std::string&)> release_callback);
    /**
     * @brief Destructor.
     */
    ~SourceManager();

    /**
     * @brief Configures and creates a new source processor instance.
     * @param config The configuration for the new source.
     * @param running A flag indicating if the audio engine is running.
     * @return A unique instance ID for the newly created source processor, or an empty string on failure.
     */
    std::string configure_source(const SourceConfig& config, bool running);
    /**
     * @brief Removes an existing source processor instance.
     * @param instance_id The unique ID of the source processor to remove.
     * @return true if the source was removed successfully, false otherwise.
     */
    bool remove_source(const std::string& instance_id);

    /** @brief Gets a reference to the map of active source processors. */
    std::map<std::string, std::unique_ptr<SourceInputProcessor>>& get_sources();
    /** @brief Gets a reference to the map of source-to-sink chunk queues. */
    std::map<std::string, std::shared_ptr<ChunkQueue>>& get_source_to_sink_queues();
    /** @brief Gets a reference to the map of command queues for sources. */
    std::map<std::string, std::shared_ptr<CommandQueue>>& get_command_queues();

    /**
     * @brief Gets a vector of pointers to all active source processors.
     * @return A vector of `SourceInputProcessor` pointers.
     */
    std::vector<SourceInputProcessor*> get_all_processors();

private:
    /**
     * @brief Generates a unique identifier for a new source processor instance.
     * @param base_tag The source tag to use as a base for the ID.
     * @return A unique string ID.
     */
    std::string generate_unique_instance_id(const std::string& base_tag);

    std::recursive_mutex& m_manager_mutex;
    TimeshiftManager* m_timeshift_manager;
    std::shared_ptr<screamrouter::audio::AudioEngineSettings> m_settings;

    std::map<std::string, std::unique_ptr<SourceInputProcessor>> m_sources;
    std::map<std::string, std::shared_ptr<PacketQueue>> m_rtp_to_source_queues;
    std::map<std::string, std::shared_ptr<ChunkQueue>> m_source_to_sink_queues;
    std::map<std::string, std::shared_ptr<CommandQueue>> m_command_queues;
    std::map<std::string, std::string> m_instance_to_capture_tag;  // Maps instance_id -> system audio capture device tag
    
    // Callbacks for system audio capture device management
    std::function<bool(const std::string&)> m_ensure_capture_callback;
    std::function<void(const std::string&)> m_release_capture_callback;
};

} // namespace audio
} // namespace screamrouter

#endif // SOURCE_MANAGER_H
