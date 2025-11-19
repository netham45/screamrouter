#include "control_api_manager.h"
#include "../utils/cpp_logger.h"
#include "../utils/lock_guard_profiler.h"
#include "../input_processor/timeshift_manager.h"

namespace screamrouter {
namespace audio {

ControlApiManager::ControlApiManager(
    std::recursive_mutex& manager_mutex,
    std::map<std::string, std::shared_ptr<CommandQueue>>& command_queues,
    TimeshiftManager* timeshift_manager,
    std::map<std::string, std::unique_ptr<SourceInputProcessor>>& sources)
    : m_manager_mutex(manager_mutex),
      m_command_queues(command_queues),
      m_timeshift_manager(timeshift_manager),
      m_sources(sources) {
    LOG_CPP_INFO("ControlApiManager created.");
}

ControlApiManager::~ControlApiManager() {
    LOG_CPP_INFO("ControlApiManager destroyed.");
}

void ControlApiManager::update_source_parameters(const std::string& instance_id, SourceParameterUpdates params, bool running) {
    std::scoped_lock lock(m_manager_mutex);
    if (!running) return;

    if (params.volume.has_value()) {
        update_source_volume_nolock(instance_id, params.volume.value());
    }
    if (params.eq_values.has_value()) {
        update_source_equalizer_nolock(instance_id, params.eq_values.value());
    }
    if (params.eq_normalization.has_value()) {
        update_source_eq_normalization_nolock(instance_id, params.eq_normalization.value());
    }
    if (params.volume_normalization.has_value()) {
        update_source_volume_normalization_nolock(instance_id, params.volume_normalization.value());
    }
    if (params.delay_ms.has_value()) {
        update_source_delay_nolock(instance_id, params.delay_ms.value());
    }
    if (params.timeshift_sec.has_value()) {
        update_source_timeshift_nolock(instance_id, params.timeshift_sec.value());
    }
    if (params.speaker_layouts_map.has_value()) {
        update_source_speaker_layouts_map_nolock(instance_id, params.speaker_layouts_map.value());
    }
}

bool ControlApiManager::send_command_to_source_nolock(const std::string& instance_id, const ControlCommand& command) {
    auto it = m_command_queues.find(instance_id);
    if (it == m_command_queues.end()) {
        return false;
    }
    it->second->push(command);
    return true;
}

void ControlApiManager::update_source_volume_nolock(const std::string& instance_id, float volume) {
    ControlCommand cmd;
    cmd.type = CommandType::SET_VOLUME;
    cmd.float_value = volume;
    send_command_to_source_nolock(instance_id, cmd);
}

void ControlApiManager::update_source_equalizer_nolock(const std::string& instance_id, const std::vector<float>& eq_values) {
    if (eq_values.size() == EQ_BANDS) {
        ControlCommand cmd;
        cmd.type = CommandType::SET_EQ;
        cmd.eq_values = eq_values;
        send_command_to_source_nolock(instance_id, cmd);
    }
}

void ControlApiManager::update_source_eq_normalization_nolock(const std::string& instance_id, bool enabled) {
    ControlCommand cmd;
    cmd.type = CommandType::SET_EQ_NORMALIZATION;
    cmd.int_value = enabled ? 1 : 0;
    send_command_to_source_nolock(instance_id, cmd);
}

void ControlApiManager::update_source_volume_normalization_nolock(const std::string& instance_id, bool enabled) {
    ControlCommand cmd;
    cmd.type = CommandType::SET_VOLUME_NORMALIZATION;
    cmd.int_value = enabled ? 1 : 0;
    send_command_to_source_nolock(instance_id, cmd);
}

void ControlApiManager::update_source_delay_nolock(const std::string& instance_id, int delay_ms) {
    ControlCommand cmd;
    cmd.type = CommandType::SET_DELAY;
    cmd.int_value = delay_ms;
    send_command_to_source_nolock(instance_id, cmd);

    if (m_timeshift_manager) {
        m_timeshift_manager->update_processor_delay(instance_id, delay_ms);
    }
}

void ControlApiManager::update_source_timeshift_nolock(const std::string& instance_id, float timeshift_sec) {
    ControlCommand cmd;
    cmd.type = CommandType::SET_TIMESHIFT;
    cmd.float_value = timeshift_sec;
    send_command_to_source_nolock(instance_id, cmd);

    if (m_timeshift_manager) {
        m_timeshift_manager->update_processor_timeshift(instance_id, timeshift_sec);
    }
}

void ControlApiManager::update_source_speaker_layouts_map_nolock(const std::string& instance_id, const std::map<int, CppSpeakerLayout>& layouts_map) {
    auto source_it = m_sources.find(instance_id);
    if (source_it != m_sources.end() && source_it->second) {
        source_it->second->set_speaker_layouts_config(layouts_map);
    } else {
        LOG_CPP_ERROR("SourceInputProcessor instance not found for speaker_layouts_map update: %s", instance_id.c_str());
    }
}


bool ControlApiManager::write_plugin_packet(
    const std::string& source_instance_tag,
    const std::vector<uint8_t>& audio_payload,
    int channels,
    int sample_rate,
    int bit_depth,
    uint8_t chlayout1,
    uint8_t chlayout2,
    bool running)
{
    if (!running) {
        LOG_CPP_ERROR("ControlApiManager not running. Cannot write plugin packet.");
        return false;
    }

    // Find the SourceInputProcessor by its original tag (passed as source_instance_tag parameter)
    SourceInputProcessor* target_processor_ptr = nullptr;
    // Iterate over the sources map to find a processor whose configured tag matches source_instance_tag
    for (const auto& pair : m_sources) {
        if (pair.second) { // Check if the unique_ptr is valid
            const auto& proc_config = pair.second->get_config(); // Get the processor's configuration
            if (proc_config.source_tag == source_instance_tag) { // Compare with the provided tag (parameter)
                target_processor_ptr = pair.second.get(); // Get raw pointer to the processor
                break;                                    // Found, exit loop
            }
        }
    }

    if (!target_processor_ptr) {
        LOG_CPP_ERROR("SourceInputProcessor instance not found for tag: %s", source_instance_tag.c_str());
        return false;
    }

    uint32_t assigned_rtp_timestamp = 0;
    {
        std::lock_guard<std::mutex> lock(m_plugin_rtp_mutex);
        const int bytes_per_sample = (bit_depth > 0 && (bit_depth % 8) == 0) ? (bit_depth / 8) : 0;
        const int bytes_per_frame = (channels > 0 && bytes_per_sample > 0) ? (channels * bytes_per_sample) : 0;

        uint32_t frame_count = 0;
        if (bytes_per_frame > 0) {
            frame_count = static_cast<uint32_t>(audio_payload.size() / static_cast<size_t>(bytes_per_frame));
        }
        if (frame_count == 0) {
            frame_count = 1; // Fallback to ensure timestamp advances even on malformed packets.
        }

        uint32_t& counter = m_plugin_rtp_counters[source_instance_tag];
        counter += frame_count;
        assigned_rtp_timestamp = counter;
    }

    // The 'source_instance_tag' passed to write_plugin_packet is the 'source_tag'
    // that TimeshiftManager will use for filtering.
    if (m_timeshift_manager) {
        TaggedAudioPacket packet;
        packet.source_tag = source_instance_tag;
        packet.received_time = std::chrono::steady_clock::now();
        packet.sample_rate = sample_rate;
        packet.bit_depth = bit_depth;
        packet.channels = channels;
        packet.chlayout1 = chlayout1;
        packet.chlayout2 = chlayout2;
        packet.audio_data = audio_payload;
        packet.rtp_timestamp = assigned_rtp_timestamp;
        m_timeshift_manager->add_packet(std::move(packet));
    } else {
        LOG_CPP_ERROR("TimeshiftManager is null. Cannot inject plugin packet.");
        return false;
    }

    return true; // Assume success if the call is made.
}

} // namespace audio
} // namespace screamrouter
