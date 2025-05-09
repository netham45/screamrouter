#ifndef RAW_SCREAM_RECEIVER_H
#define RAW_SCREAM_RECEIVER_H

#include "audio_component.h"
#include "thread_safe_queue.h"
#include "audio_types.h"
#include "rtp_receiver.h" // Include to get SourceOutputTarget definition

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <set>


namespace screamrouter {
namespace audio {

// Define socket types inside the namespace
#ifdef _WIN32
    using socket_t = SOCKET;
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
#else // POSIX
    using socket_t = int;
    #define INVALID_SOCKET_VALUE -1
#endif

// Using aliases from audio_types.h or RtpReceiver.h if applicable
using NotificationQueue = utils::ThreadSafeQueue<NewSourceNotification>;
using PacketQueue = utils::ThreadSafeQueue<TaggedAudioPacket>;

// Removed duplicate definitions of SourceOutputTarget and OutputTargetMap
// They are now included from rtp_receiver.h


class RawScreamReceiver : public AudioComponent {
public:
    RawScreamReceiver(
        RawScreamReceiverConfig config,
        std::shared_ptr<NotificationQueue> notification_queue
    );

    ~RawScreamReceiver() noexcept; // Added noexcept

    // --- AudioComponent Interface ---
    void start() override;
    void stop() override;

    // --- RawScreamReceiver Specific ---
    void add_output_queue(
        const std::string& source_tag,
        const std::string& instance_id,
        std::shared_ptr<PacketQueue> queue
        // Removed mutex and cv parameters
    );

    void remove_output_queue(const std::string& source_tag, const std::string& instance_id);

protected:
    // --- AudioComponent Interface ---
    void run() override; // The main thread loop

private:
    RawScreamReceiverConfig config_;
    socket_t socket_fd_; // Use cross-platform type, initialize in constructor
    std::shared_ptr<NotificationQueue> notification_queue_;

    OutputTargetMap output_targets_;
    std::mutex targets_mutex_;

    std::set<std::string> known_source_tags_;
    std::mutex known_tags_mutex_;

    // Internal helper methods
    bool setup_socket();
    void close_socket();
    bool is_valid_raw_scream_packet(const uint8_t* buffer, ssize_t size);
};

} // namespace audio
} // namespace screamrouter

#endif // RAW_SCREAM_RECEIVER_H
