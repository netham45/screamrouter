#ifndef RTP_RECEIVER_H
#define RTP_RECEIVER_H

#include "audio_component.h"
#include "thread_safe_queue.h"
#include "audio_types.h"

#include <string>
#include <vector>
#include <map>
#include <memory> // For shared_ptr
#include <mutex>
#include <condition_variable>
#include <set> // For known source tags
#include <vector> // For vector of output targets

// Forward declaration
namespace screamrouter { namespace utils { template <typename T> class ThreadSafeQueue; } }

// Socket related includes
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h> // For sockaddr_in
#include <arpa/inet.h>  // For inet_ntoa
#include <unistd.h>     // For close
#include <poll.h>       // For poll

namespace screamrouter {
namespace audio {

// Using alias for clarity
using NotificationQueue = utils::ThreadSafeQueue<NewSourceNotification>;
using PacketQueue = utils::ThreadSafeQueue<TaggedAudioPacket>;

// Forward declare SourceInputProcessor to avoid circular include if needed later
// class SourceInputProcessor; // Not strictly needed if just passing pointers

// Struct to hold queue and synchronization primitives for ONE source processor instance
struct SourceOutputTarget {
    std::shared_ptr<PacketQueue> queue; // The specific queue for this processor instance
    std::mutex* processor_mutex = nullptr; // Pointer to its timeshift_mutex_
    std::condition_variable* processor_cv = nullptr; // Pointer to its timeshift_condition_

    // Overload equality operator to find targets based on queue pointer for removal
    // Removed equality operator based on queue pointer as instance_id is the unique key now
};

// Map: source_tag (IP) -> instance_id -> Target Info
using OutputTargetMap = std::map<std::string, std::map<std::string, SourceOutputTarget>>;


class RtpReceiver : public AudioComponent {
public:
    RtpReceiver(
        RtpReceiverConfig config,
        std::shared_ptr<NotificationQueue> notification_queue
    );

    ~RtpReceiver() override;

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
        std::shared_ptr<PacketQueue> queue,
        std::mutex* processor_mutex,
        std::condition_variable* processor_cv
    );

    /**
     * @brief Removes the output target associated with a specific source processor instance.
     *        Called by AudioManager when a SourceInputProcessor is removed.
     * @param source_tag The source identifier (IP address).
     * @param instance_id The unique ID of the instance to remove.
     */
    void remove_output_queue(const std::string& source_tag, const std::string& instance_id); // Changed signature

protected:
    // --- AudioComponent Interface ---
    void run() override; // The main thread loop

private:
    RtpReceiverConfig config_;
    int socket_fd_ = -1;
    std::shared_ptr<NotificationQueue> notification_queue_;

    // Map: source_tag (IP) -> instance_id -> Target Info
    OutputTargetMap output_targets_;
    std::mutex targets_mutex_; // Protects access to output_targets_ map

    // Keep track of known source tags (IP addresses) to avoid spamming notifications
    // Keep track of known source tags to avoid spamming notifications
    std::set<std::string> known_source_tags_;
    std::mutex known_tags_mutex_; // Protects access to known_source_tags_

    // Internal helper methods
    bool setup_socket();
    void close_socket();
    /**
     * @brief Parses the RTP header to check for validity (payload type).
     * @param buffer Pointer to the start of the received UDP payload.
     * @param size Size of the received payload.
     * @return true if the header indicates a valid Scream packet (payload type 127), false otherwise.
     */
    bool is_valid_rtp_payload(const uint8_t* buffer, ssize_t size);
};

} // namespace audio
} // namespace screamrouter

#endif // RTP_RECEIVER_H
