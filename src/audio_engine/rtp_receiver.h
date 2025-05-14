#ifndef RTP_RECEIVER_H
#define RTP_RECEIVER_H

// Include standard headers first
#include <string>
#include <vector>
#include <map> // Include map early
#include <memory>
#include <mutex>
#include <condition_variable>
#include <set>

// Define platform-specific socket types and macros
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "Ws2_32.lib")
    using socket_t = SOCKET;
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
    #define poll WSAPoll
    #define GET_LAST_SOCK_ERROR WSAGetLastError()
#else // POSIX
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <poll.h>
    #include <errno.h>
    using socket_t = int;
    #define INVALID_SOCKET_VALUE -1
    #define GET_LAST_SOCK_ERROR errno
#endif

#include "audio_component.h"
#include "thread_safe_queue.h"
#include "audio_types.h"
#include "timeshift_manager.h" // Added for TimeshiftManager

// Other includes seem fine here now

#include <condition_variable>
#include <set> // For known source tags
#include <vector> // For vector of output targets

// Forward declaration
namespace screamrouter { namespace utils { template <typename T> class ThreadSafeQueue; } }


namespace screamrouter {
namespace audio {


// Using alias for clarity
using NotificationQueue = utils::ThreadSafeQueue<NewSourceNotification>;
// PacketQueue is defined in TimeshiftManager.h or audio_types.h, ensure it's compatible.
// using PacketQueue = utils::ThreadSafeQueue<TaggedAudioPacket>; // This might be redundant if included elsewhere

// Struct SourceOutputTarget and OutputTargetMap are removed as per Task 4


class RtpReceiver : public AudioComponent {
public:
    RtpReceiver(
        RtpReceiverConfig config,
        std::shared_ptr<NotificationQueue> notification_queue,
        TimeshiftManager* timeshift_manager // Added TimeshiftManager
    );

    ~RtpReceiver() noexcept; // Added noexcept, removed override

    // --- AudioComponent Interface ---
    void start() override;
    void stop() override;

    // add_output_queue and remove_output_queue are removed

    std::vector<std::string> get_seen_tags(); // Added

protected:
    // --- AudioComponent Interface ---
    void run() override; // The main thread loop

private:
    RtpReceiverConfig config_;
    socket_t socket_fd_; // Use cross-platform type alias, initialize in constructor
    std::shared_ptr<NotificationQueue> notification_queue_;
    TimeshiftManager* timeshift_manager_; // Added TimeshiftManager pointer

    // OutputTargetMap and targets_mutex_ are removed

    // Keep track of known source tags (IP addresses) to avoid spamming notifications
    // Keep track of known source tags to avoid spamming notifications
    std::set<std::string> known_source_tags_;
    std::mutex known_tags_mutex_; // Protects access to known_source_tags_

    std::vector<std::string> seen_tags_; // Added
    std::mutex seen_tags_mutex_;      // Added

    // Internal helper methods
    bool setup_socket();
    void close_socket();
    /**
     * @brief Parses the RTP header to check for validity (payload type).
     * @param buffer Pointer to the start of the received UDP payload.
     * @param size Size of the received payload.
     * @return true if the header indicates a valid Scream packet (payload type 127), false otherwise.
     */
    bool is_valid_rtp_payload(const uint8_t* buffer, int size);
};

} // namespace audio
} // namespace screamrouter

#endif // RTP_RECEIVER_H
