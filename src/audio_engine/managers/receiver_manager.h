#ifndef RECEIVER_MANAGER_H
#define RECEIVER_MANAGER_H

#include "../receivers/rtp/rtp_receiver.h"
#include "../receivers/scream/raw_scream_receiver.h"
#include "../receivers/scream/per_process_scream_receiver.h"
#include "../utils/thread_safe_queue.h"
#include "../input_processor/timeshift_manager.h"
#include "../audio_types.h"
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <mutex>

namespace screamrouter {
namespace audio {

class ReceiverManager {
public:
    ReceiverManager(std::mutex& manager_mutex, TimeshiftManager* timeshift_manager);
    ~ReceiverManager();

    bool initialize_receivers(int rtp_listen_port, std::shared_ptr<NotificationQueue> notification_queue);
    void start_receivers();
    void stop_receivers();
    void cleanup_receivers();

    std::vector<std::string> get_rtp_receiver_seen_tags();
    std::vector<std::string> get_raw_scream_receiver_seen_tags(int listen_port);
    std::vector<std::string> get_per_process_scream_receiver_seen_tags(int listen_port);

private:
    std::mutex& m_manager_mutex;
    TimeshiftManager* m_timeshift_manager;

    std::unique_ptr<RtpReceiver> m_rtp_receiver;
    std::map<int, std::unique_ptr<RawScreamReceiver>> m_raw_scream_receivers;
    std::map<int, std::unique_ptr<PerProcessScreamReceiver>> m_per_process_scream_receivers;
};

} // namespace audio
} // namespace screamrouter

#endif // RECEIVER_MANAGER_H