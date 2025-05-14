#ifndef PER_PROCESS_SCREAM_RECEIVER_H
#define PER_PROCESS_SCREAM_RECEIVER_H

#include "audio_component.h"
#include "thread_safe_queue.h"
#include "audio_types.h" // For PerProcessScreamReceiverConfig, NewSourceNotification, TaggedAudioPacket
// #include "rtp_receiver.h" // No longer needed for OutputTargetMap
#include "timeshift_manager.h" // Added for TimeshiftManager

// Platform-specific socket includes
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "Ws2_32.lib")
    #define GET_LAST_SOCK_ERROR_PPSR WSAGetLastError() // Specific macro
#else // POSIX
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h> // For close
    #include <poll.h>   // For poll
    #include <errno.h>
    #define GET_LAST_SOCK_ERROR_PPSR errno // Specific macro
#endif

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
    // INVALID_SOCKET is defined in winsock2.h
    // #define INVALID_SOCKET_VALUE_PPSR INVALID_SOCKET // Redundant
#else // POSIX
    using socket_t = int;
    #define INVALID_SOCKET_VALUE_PPSR -1 // Keep this for POSIX
#endif

// Using aliases from audio_types.h or RtpReceiver.h if applicable
using NotificationQueue = utils::ThreadSafeQueue<NewSourceNotification>;
using PacketQueue = utils::ThreadSafeQueue<TaggedAudioPacket>;

// OutputTargetMap and SourceOutputTarget are no longer used here.

class PerProcessScreamReceiver : public AudioComponent {
public:
    PerProcessScreamReceiver(
        PerProcessScreamReceiverConfig config,
        std::shared_ptr<NotificationQueue> notification_queue,
        TimeshiftManager* timeshift_manager // Added TimeshiftManager
    );

    ~PerProcessScreamReceiver() noexcept;

    // --- AudioComponent Interface ---
    void start() override;
    void stop() override;

    // add_output_queue and remove_output_queue are removed

    std::vector<std::string> get_seen_tags(); // Added

protected:
    // --- AudioComponent Interface ---
    void run() override; // The main thread loop

private:
    PerProcessScreamReceiverConfig config_;
    socket_t socket_fd_;
    std::shared_ptr<NotificationQueue> notification_queue_;
    TimeshiftManager* timeshift_manager_; // Added TimeshiftManager pointer

    // OutputTargetMap and targets_mutex_ are removed

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
