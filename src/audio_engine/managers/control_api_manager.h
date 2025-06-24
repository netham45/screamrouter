#ifndef CONTROL_API_MANAGER_H
#define CONTROL_API_MANAGER_H

#include "../audio_types.h"
#include "../input_processor/source_input_processor.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>

namespace screamrouter {
namespace audio {

class TimeshiftManager;

class ControlApiManager {
public:
    ControlApiManager(
        std::mutex& manager_mutex,
        std::map<std::string, std::shared_ptr<CommandQueue>>& command_queues,
        TimeshiftManager* timeshift_manager,
        std::map<std::string, std::unique_ptr<SourceInputProcessor>>& sources
    );
    ~ControlApiManager();

    void update_source_parameters(const std::string& instance_id, SourceParameterUpdates params, bool running);
    bool send_command_to_source(const std::string& instance_id, const ControlCommand& command, bool running);

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
    bool send_command_to_source_nolock(const std::string& instance_id, const ControlCommand& command);
    void update_source_volume_nolock(const std::string& instance_id, float volume);
    void update_source_equalizer_nolock(const std::string& instance_id, const std::vector<float>& eq_values);
    void update_source_eq_normalization_nolock(const std::string& instance_id, bool enabled);
    void update_source_volume_normalization_nolock(const std::string& instance_id, bool enabled);
    void update_source_delay_nolock(const std::string& instance_id, int delay_ms);
    void update_source_timeshift_nolock(const std::string& instance_id, float timeshift_sec);
    void update_source_speaker_layouts_map_nolock(const std::string& instance_id, const std::map<int, CppSpeakerLayout>& layouts_map);

    std::mutex& m_manager_mutex;
    std::map<std::string, std::shared_ptr<CommandQueue>>& m_command_queues;
    TimeshiftManager* m_timeshift_manager;
    std::map<std::string, std::unique_ptr<SourceInputProcessor>>& m_sources;
};

} // namespace audio
} // namespace screamrouter

#endif // CONTROL_API_MANAGER_H