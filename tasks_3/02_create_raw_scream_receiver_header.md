# Task 2: Create RawScreamReceiver Header File

**Goal**: Define the `RawScreamReceiver` class interface in a new header file.

**Files to Create**:
*   `src/audio_engine/raw_scream_receiver.h`

**Steps**:

1.  **Create the file** `src/audio_engine/raw_scream_receiver.h`.
2.  **Add include guards** (`#ifndef`, `#define`, `#endif`).
3.  **Include necessary headers**:
    *   `audio_component.h` (base class)
    *   `thread_safe_queue.h`
    *   `audio_types.h` (for `RawScreamReceiverConfig`, `NewSourceNotification`, `TaggedAudioPacket`, `PacketQueue`, `NotificationQueue`)
    *   Standard C++ headers (`string`, `vector`, `map`, `memory`, `mutex`, `condition_variable`, `set`).
    *   Socket headers (`sys/types.h`, `sys/socket.h`, `netinet/in.h`, `arpa/inet.h`, `unistd.h`, `poll.h`).
4.  **Define the `RawScreamReceiver` class**:
    *   Inherit publicly from `AudioComponent`.
    *   Declare the constructor taking `RawScreamReceiverConfig` and `std::shared_ptr<NotificationQueue>`.
    *   Declare the virtual destructor `~RawScreamReceiver() override;`.
    *   Declare the overridden `start()` and `stop()` methods.
    *   Declare public methods for managing output queues (similar to `RtpReceiver`):
        *   `add_output_queue(...)`
        *   `remove_output_queue(...)`
    *   Declare the protected overridden `run()` method.
    *   Declare private members:
        *   `RawScreamReceiverConfig config_;`
        *   `int socket_fd_ = -1;`
        *   `std::shared_ptr<NotificationQueue> notification_queue_;`
        *   `OutputTargetMap output_targets_;` (Use the same type alias as `RtpReceiver` or define locally)
        *   `std::mutex targets_mutex_;`
        *   `std::set<std::string> known_source_tags_;`
        *   `std::mutex known_tags_mutex_;`
    *   Declare private helper methods:
        *   `bool setup_socket();`
        *   `void close_socket();`
        *   `bool is_valid_raw_scream_packet(const uint8_t* buffer, int size);` (Checks size == 1157, maybe basic header checks if desired)

```cpp
// Example Structure (src/audio_engine/raw_scream_receiver.h)

#ifndef RAW_SCREAM_RECEIVER_H
#define RAW_SCREAM_RECEIVER_H

#include "audio_component.h"
#include "thread_safe_queue.h"
#include "audio_types.h"

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <set>

// Socket related includes
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>

namespace screamrouter {
namespace audio {

// Using aliases from audio_types.h or RtpReceiver.h if applicable
using NotificationQueue = utils::ThreadSafeQueue<NewSourceNotification>;
using PacketQueue = utils::ThreadSafeQueue<TaggedAudioPacket>;

// Struct to hold queue and synchronization primitives for ONE source processor instance
// (Can reuse the definition from RtpReceiver.h or define here if kept separate)
struct SourceOutputTarget {
    std::shared_ptr<PacketQueue> queue;
    std::mutex* processor_mutex = nullptr;
    std::condition_variable* processor_cv = nullptr;
};

// Map: source_tag (IP) -> instance_id -> Target Info
using OutputTargetMap = std::map<std::string, std::map<std::string, SourceOutputTarget>>;


class RawScreamReceiver : public AudioComponent {
public:
    RawScreamReceiver(
        RawScreamReceiverConfig config,
        std::shared_ptr<NotificationQueue> notification_queue
    );

    ~RawScreamReceiver() override;

    // --- AudioComponent Interface ---
    void start() override;
    void stop() override;

    // --- RawScreamReceiver Specific ---
    void add_output_queue(
        const std::string& source_tag,
        const std::string& instance_id,
        std::shared_ptr<PacketQueue> queue,
        std::mutex* processor_mutex,
        std::condition_variable* processor_cv
    );

    void remove_output_queue(const std::string& source_tag, const std::string& instance_id);

protected:
    // --- AudioComponent Interface ---
    void run() override; // The main thread loop

private:
    RawScreamReceiverConfig config_;
    int socket_fd_ = -1;
    std::shared_ptr<NotificationQueue> notification_queue_;

    OutputTargetMap output_targets_;
    std::mutex targets_mutex_;

    std::set<std::string> known_source_tags_;
    std::mutex known_tags_mutex_;

    // Internal helper methods
    bool setup_socket();
    void close_socket();
    bool is_valid_raw_scream_packet(const uint8_t* buffer, int size);
};

} // namespace audio
} // namespace screamrouter

#endif // RAW_SCREAM_RECEIVER_H
