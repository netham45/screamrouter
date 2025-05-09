#ifndef PER_PROCESS_SCREAM_RECEIVER_H
#define PER_PROCESS_SCREAM_RECEIVER_H

#include "audio_component.h"
#include "thread_safe_queue.h"
#include "audio_types.h" // For PerProcessScreamReceiverConfig, NewSourceNotification, TaggedAudioPacket
#include "rtp_receiver.h"    // For OutputTargetMap, SourceOutputTarget

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <set>

// PerProcessScreamReceiverConfig is now defined in audio_types.h

namespace screamrouter {
namespace audio {

// Define socket types inside the namespace (consistent with RawScreamReceiver)
#ifdef _WIN32
    using socket_t = SOCKET;
    #define INVALID_SOCKET_VALUE_PPSR INVALID_SOCKET // Use a distinct macro to avoid redefinition if headers are combined
#else // POSIX
    using socket_t = int;
    #define INVALID_SOCKET_VALUE_PPSR -1
#endif

// Using aliases from audio_types.h or RtpReceiver.h if applicable
using NotificationQueue = utils::ThreadSafeQueue<NewSourceNotification>;
using PacketQueue = utils::ThreadSafeQueue<TaggedAudioPacket>;

// OutputTargetMap and SourceOutputTarget are included from rtp_receiver.h

class PerProcessScreamReceiver : public AudioComponent {
public:
    PerProcessScreamReceiver(
        PerProcessScreamReceiverConfig config,
        std::shared_ptr<NotificationQueue> notification_queue
    );

    ~PerProcessScreamReceiver() noexcept;

    // --- AudioComponent Interface ---
    void start() override;
    void stop() override;

    // --- PerProcessScreamReceiver Specific ---
    void add_output_queue(
        const std::string& source_tag, // Composite tag: IP_ProgramTag
        const std::string& instance_id,
        std::shared_ptr<PacketQueue> queue
    );

    void remove_output_queue(const std::string& source_tag, const std::string& instance_id);

    std::vector<std::string> get_seen_tags(); // Added

protected:
    // --- AudioComponent Interface ---
    void run() override; // The main thread loop

private:
    PerProcessScreamReceiverConfig config_;
    socket_t socket_fd_;
    std::shared_ptr<NotificationQueue> notification_queue_;

    OutputTargetMap output_targets_; // Maps composite_source_tag to map of instance_id to target queue
    std::mutex targets_mutex_;

    std::set<std::string> known_source_tags_; // Stores composite_source_tag
    std::mutex known_tags_mutex_;

    std::vector<std::string> seen_tags_;
    std::mutex seen_tags_mutex_;

    // Internal helper methods
    bool setup_socket();
    void close_socket();
    bool is_valid_per_process_scream_packet(const uint8_t* buffer, int size);
};

} // namespace audio
} // namespace screamrouter

#endif // PER_PROCESS_SCREAM_RECEIVER_H
