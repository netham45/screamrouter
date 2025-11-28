/**
 * @file control_api_manager.h
 * @brief Defines the ControlApiManager class for handling real-time control of audio sources.
 * @details This class provides the logic for dispatching control commands (like volume, EQ,
 *          and delay changes) to the appropriate SourceInputProcessor instances via their
 *          command queues.
 */
#ifndef CONTROL_API_MANAGER_H
#define CONTROL_API_MANAGER_H

#include "../audio_types.h"
#include "../input_processor/source_input_processor.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <cstdint>

namespace screamrouter {
namespace audio {

class TimeshiftManager;

/**
 * @class ControlApiManager
 * @brief Manages the dispatch of control commands to audio source processors.
 * @details This class acts as a centralized point for updating the parameters of
 *          running `SourceInputProcessor` instances. It takes high-level parameter
 *          update requests and translates them into specific `ControlCommand` objects
 *          that are then pushed onto the appropriate command queues.
 */
class ControlApiManager {
public:
    /**
     * @brief Constructs a ControlApiManager.
     * @param manager_mutex A reference to the main AudioManager mutex for thread safety.
     * @param timeshift_manager A pointer to the TimeshiftManager for updating delay/timeshift.
     * @param sources A reference to the map of active source processors.
     */
    ControlApiManager(
        std::recursive_mutex& manager_mutex,
        TimeshiftManager* timeshift_manager,
        std::map<std::string, std::unique_ptr<SourceInputProcessor>>& sources
    );
    /**
     * @brief Destructor.
     */
    ~ControlApiManager();

    /**
     * @brief Atomically updates multiple parameters for a source processor.
     * @param instance_id The ID of the source processor instance to update.
     * @param params A struct containing the optional parameters to update.
     * @param running A flag indicating if the audio engine is running.
     */
    void update_source_parameters(const std::string& instance_id, SourceParameterUpdates params, bool running);

    /**
     * @brief Injects a plugin-generated audio packet into a source processor.
     * @param source_instance_tag The tag of the target source processor.
     * @param audio_payload The raw audio data.
     * @param channels Number of audio channels.
     * @param sample_rate Sample rate in Hz.
     * @param bit_depth Bit depth.
     * @param chlayout1 Scream channel layout byte 1.
     * @param chlayout2 Scream channel layout byte 2.
     * @param running A flag indicating if the audio engine is running.
     * @return true if the packet was injected successfully, false otherwise.
     */
    bool write_plugin_packet(
        const std::string& source_instance_tag,
        const std::vector<uint8_t>& audio_payload,
        int channels,
        int sample_rate,
        int bit_depth,
        uint8_t chlayout1,
        uint8_t chlayout2,
        bool running
    );

private:
    SourceInputProcessor* find_source_nolock(const std::string& instance_id);
    void update_source_volume_nolock(const std::string& instance_id, float volume);
    void update_source_equalizer_nolock(const std::string& instance_id, const std::vector<float>& eq_values);
    void update_source_eq_normalization_nolock(const std::string& instance_id, bool enabled);
    void update_source_volume_normalization_nolock(const std::string& instance_id, bool enabled);
    void update_source_delay_nolock(const std::string& instance_id, int delay_ms);
    void update_source_timeshift_nolock(const std::string& instance_id, float timeshift_sec);
    void update_source_speaker_layouts_map_nolock(const std::string& instance_id, const std::map<int, CppSpeakerLayout>& layouts_map);

    std::recursive_mutex& m_manager_mutex;
    TimeshiftManager* m_timeshift_manager;
    std::map<std::string, std::unique_ptr<SourceInputProcessor>>& m_sources;
    std::unordered_map<std::string, uint32_t> m_plugin_rtp_counters;
    std::mutex m_plugin_rtp_mutex;
};

} // namespace audio
} // namespace screamrouter

#endif // CONTROL_API_MANAGER_H
