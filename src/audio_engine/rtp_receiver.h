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
using PacketQueue = utils::ThreadSafeQueue<TaggedAudioPacket>;

// Forward declare SourceInputProcessor to avoid circular include if needed later
// class SourceInputProcessor; // Not strictly needed if just passing pointers

// Struct to hold queue for ONE source processor instance
struct SourceOutputTarget {
    std::shared_ptr<PacketQueue> queue; // The specific queue for this processor instance
    // Removed mutex and cv pointers
};

// Map: source_tag (IP) -> instance_id -> Target Info
using OutputTargetMap = std::map<std::string, std::map<std::string, SourceOutputTarget>>;


class RtpReceiver : public AudioComponent {
public:
    RtpReceiver(
        RtpReceiverConfig config,
        std::shared_ptr<NotificationQueue> notification_queue
    );

    ~RtpReceiver() noexcept; // Added noexcept, removed override

    // --- AudioComponent Interface ---
    void start() override;
    void stop() override;

    // --- RtpReceiver Specific ---
    /**
     * @brief Registers an output target (queue and sync primitives) for a specific source processor instance
     *        associated with a source tag. Allows multiple targets per tag.
     *        Called by AudioManager when a new SourceInputProcessor is created.
     * @param source_tag The identifier (IP address) of the source.
     * @param queue The specific thread-safe queue for this processor instance.
     * @param source_tag The source identifier (IP address) from which packets originate.
     * @param instance_id The unique ID of the specific SourceInputProcessor instance.
     * @param queue The specific thread-safe queue for this processor instance.
     * @param processor_mutex Pointer to the processor's timeshift mutex.
     * @param processor_cv Pointer to the processor's timeshift condition variable.
     */
    void add_output_queue(
        const std::string& source_tag,
        const std::string& instance_id, // Added instance_id
        std::shared_ptr<PacketQueue> queue
        // Removed mutex and cv parameters
    );

    /**
     * @brief Removes the output target associated with a specific source processor instance.
     *        Called by AudioManager when a SourceInputProcessor is removed.
     * @param source_tag The source identifier (IP address).
     * @param instance_id The unique ID of the instance to remove.
     */
    void remove_output_queue(const std::string& source_tag, const std::string& instance_id); // Changed signature

    std::vector<std::string> get_seen_tags(); // Added

protected:
    // --- AudioComponent Interface ---
    void run() override; // The main thread loop

private:
    RtpReceiverConfig config_;
    socket_t socket_fd_; // Use cross-platform type alias, initialize in constructor
    std::shared_ptr<NotificationQueue> notification_queue_;

    // Map: source_tag (IP) -> instance_id -> Target Info
    OutputTargetMap output_targets_;
    std::mutex targets_mutex_; // Protects access to output_targets_ map

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
