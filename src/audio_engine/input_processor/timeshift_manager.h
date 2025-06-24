#ifndef TIMESHIFT_MANAGER_H
#define TIMESHIFT_MANAGER_H

#include "../utils/audio_component.h"
#include "../utils/thread_safe_queue.h"
#include "../audio_types.h" // For TaggedAudioPacket, PacketQueue

#include <string>
#include <vector>
#include <deque>
#include <map>
#include <chrono>
#include <memory>
#include <mutex>
#include <condition_variable>

namespace screamrouter {
namespace audio {

// Forward declaration for PacketQueue if not fully defined in audio_types.h yet for this context
// Assuming PacketQueue is std::shared_ptr<utils::ThreadSafeQueue<TaggedAudioPacket>> as per audio_types.h usage elsewhere
using PacketQueue = utils::ThreadSafeQueue<TaggedAudioPacket>;

struct ProcessorTargetInfo {
    std::shared_ptr<PacketQueue> target_queue; // This is the input queue of a SourceInputProcessor
    int current_delay_ms;
    float current_timeshift_backshift_sec;
    size_t next_packet_read_index; // Index into the global_timeshift_buffer_ for this processor
    std::string source_tag_filter; // The source_tag this processor is interested in.
};

struct StreamTimingState {
    bool is_first_packet = true;
    double jitter_estimate = 0.0; // Smoothed jitter estimate in milliseconds
    uint32_t last_rtp_timestamp = 0;
    std::chrono::steady_clock::time_point last_wallclock;
};

class TimeshiftManager : public AudioComponent {
public:
    TimeshiftManager(std::chrono::seconds max_buffer_duration);
    ~TimeshiftManager() override;

    void start() override;
    void stop() override;

    void add_packet(TaggedAudioPacket&& packet);
    void register_processor(const std::string& instance_id, const std::string& source_tag, std::shared_ptr<PacketQueue> target_queue, int initial_delay_ms, float initial_timeshift_sec);
    void unregister_processor(const std::string& instance_id, const std::string& source_tag);
    void update_processor_delay(const std::string& instance_id, int delay_ms);
    void update_processor_timeshift(const std::string& instance_id, float timeshift_sec);

protected:
    void run() override;

private:
    std::deque<TaggedAudioPacket> global_timeshift_buffer_;
    std::mutex buffer_mutex_; // Protects global_timeshift_buffer_ and processor_targets_'s next_packet_read_index adjustments during cleanup

    // Map: source_tag -> instance_id -> ProcessorTargetInfo
    std::map<std::string, std::map<std::string, ProcessorTargetInfo>> processor_targets_;
    std::mutex targets_mutex_; // Protects processor_targets_ structure (add/remove processors, update delays)

    // Dejittering state
    std::map<std::string, StreamTimingState> stream_timing_states_;
    std::mutex timing_mutex_;

    std::condition_variable run_loop_cv_; // To wake up the run loop
    std::chrono::seconds max_buffer_duration_sec_; // Configurable max duration for the global buffer
    std::chrono::steady_clock::time_point last_cleanup_time_;

    void processing_loop_iteration(); // Helper for run()
    void cleanup_global_buffer(); // Periodically called
};

} // namespace audio
} // namespace screamrouter

#endif // TIMESHIFT_MANAGER_H
