#include "pulse_receiver.h"

#if defined(_WIN32) || defined(_WIN64) || defined(WIN32)

#include <stdexcept>
#include <utility>

namespace screamrouter {
namespace audio {
namespace pulse {

struct PulseAudioReceiver::Impl {};

PulseAudioReceiver::PulseAudioReceiver(PulseReceiverConfig config,
                                       std::shared_ptr<NotificationQueue> notification_queue,
                                       TimeshiftManager* timeshift_manager,
                                       ClockManager* clock_manager,
                                       std::string logger_prefix)
    : config_(std::move(config)) {
    (void)notification_queue;
    (void)timeshift_manager;
    (void)clock_manager;
    (void)logger_prefix;
}

PulseAudioReceiver::~PulseAudioReceiver() = default;

void PulseAudioReceiver::start() {
    throw std::runtime_error("PulseAudio receiver is not available on Windows");
}

void PulseAudioReceiver::stop() {}

std::vector<std::string> PulseAudioReceiver::get_seen_tags() {
    return {};
}

void PulseAudioReceiver::run() {}

} // namespace pulse
} // namespace audio
} // namespace screamrouter

#else // POSIX build

#include "pulse_message.h"
#include "pulse_tagstruct.h"

#include "../../input_processor/timeshift_manager.h"
#include "../../utils/byte_ring_buffer.h"
#include "../../utils/cpp_logger.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <atomic>
#include <deque>
#include <exception>
#include <mutex>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <utility>
#include <random>

#include "../../audio_processor/audio_processor.h"

#include <cerrno>
#include <csignal>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

namespace screamrouter {
namespace audio {
namespace pulse {
namespace {

constexpr uint32_t kMaxConnections = 64;
constexpr uint32_t kVirtualSinkIndex = 0;
constexpr const char* kVirtualSinkName = "screamrouter.pulse";
constexpr const char* kVirtualSinkDescription = "ScreamRouter Virtual Pulse Sink";
constexpr uint32_t kPulseCookieLength = 256;
constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;
constexpr uint32_t kDefaultBufferLength = 48 * 1024; // 1 second @ 48kHz, 8ch, 32-bit
constexpr uint32_t kDefaultMinReq = 1152;
constexpr uint32_t kDefaultPrebuf = 0;
constexpr uint32_t kDefaultMaxLength = kDefaultBufferLength * 2;
constexpr int64_t kMaxCatchupUsecPerChunk = 50000; // limit to 20ms of catch-up per chunk to avoid pops
constexpr int64_t kMaxUnderrunResetUsec = 500000;  // jump directly to realtime if we fall >500ms behind
constexpr uint32_t kProgramTagLength = 30;
constexpr uint32_t kPaddedIpLength = 32;
constexpr uint32_t kVolumeNorm = 0x10000u;
constexpr uint8_t kSampleFormatS32LE = 7; // matches PulseAudio's PA_SAMPLE_S32LE
constexpr uint8_t kSampleFormatS16LE = 3;
constexpr uint8_t kSampleFormatFloat32LE = 5;
constexpr uint8_t kChannelLayoutMono = 0x01;
constexpr uint8_t kChannelLayoutStereo = 0x03;
constexpr uint32_t kDescriptorFlagShmData = 0x80000000u;
constexpr uint32_t kDescriptorFlagShmRelease = 0x40000000u;
constexpr uint32_t kDescriptorFlagShmRevoke = 0xC0000000u;
constexpr uint32_t kDescriptorFlagShmWritable = 0x00800000u;
constexpr uint32_t kDescriptorFlagMemfdBlock = 0x20000000u;
constexpr size_t kMaxAncillaryFds = 8;
constexpr size_t kShmInfoBlockIdIndex = 0;
constexpr size_t kShmInfoShmIdIndex = 1;
constexpr size_t kShmInfoOffsetIndex = 2;
constexpr size_t kShmInfoLengthIndex = 3;
constexpr uint32_t kUpdateSet = 0;
constexpr uint32_t kUpdateMerge = 1;
constexpr uint32_t kUpdateReplace = 2;

inline uint32_t sanitize_buffer_value(uint32_t value, uint32_t fallback) {
    if (value == 0 || value == static_cast<uint32_t>(-1)) {
        return fallback;
    }
    return value;
}

constexpr uint32_t PA_ERR_ACCESS = 1;
constexpr uint32_t PA_ERR_PROTOCOL = 7;
constexpr uint32_t PA_ERR_INVALID = 3;
constexpr uint32_t PA_ERR_NOENTITY = 5;
constexpr uint32_t PA_ERR_NOTSUPPORTED = 19;

std::atomic<uint64_t> g_pulse_stream_counter{0};

const char* command_name(Command c) {
    switch (c) {
        case Command::Auth: return "Auth";
        case Command::SetClientName: return "SetClientName";
        case Command::GetServerInfo: return "GetServerInfo";
        case Command::Subscribe: return "Subscribe";
        case Command::LookupSink: return "LookupSink";
        case Command::GetSinkInfo: return "GetSinkInfo";
        case Command::GetSinkInfoList: return "GetSinkInfoList";
        case Command::CreatePlaybackStream: return "CreatePlaybackStream";
        case Command::DeletePlaybackStream: return "DeletePlaybackStream";
        case Command::CorkPlaybackStream: return "CorkPlaybackStream";
        case Command::FlushPlaybackStream: return "FlushPlaybackStream";
        case Command::DrainPlaybackStream: return "DrainPlaybackStream";
        case Command::SetPlaybackStreamBufferAttr: return "SetPlaybackStreamBufferAttr";
        case Command::GetPlaybackLatency: return "GetPlaybackLatency";
        case Command::SetSinkInputVolume: return "SetSinkInputVolume";
        case Command::SetPlaybackStreamName: return "SetPlaybackStreamName";
        case Command::UpdatePlaybackStreamProplist: return "UpdatePlaybackStreamProplist";
        case Command::UpdateClientProplist: return "UpdateClientProplist";
        case Command::Request: return "Request";
        case Command::RegisterMemfdShmid: return "RegisterMemfdShmid";
        case Command::PlaybackStreamEvent: return "PlaybackStreamEvent";
        case Command::Started: return "Started";
        case Command::Exit: return "Exit";
        default: return "Other";
    }
}

inline std::string trim_string(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\n\r");
    return value.substr(first, last - first + 1);
}

inline std::string pad_or_truncate(const std::string& value, std::size_t width) {
    if (value.size() >= width) {
        return value;
    }
    std::string result(width, ' ');
    std::copy(value.begin(), value.end(), result.begin());
    return result;
}

inline void strip_nuls(std::string& value) {
    value.erase(std::remove(value.begin(), value.end(), '\0'), value.end());
}

void apply_proplist_update(std::unordered_map<std::string, std::string>& target,
                           const std::unordered_map<std::string, std::string>& update,
                           uint32_t mode) {
    switch (mode) {
        case kUpdateReplace:
            target = update;
            break;
        case kUpdateMerge:
            for (const auto& [key, value] : update) {
                target.emplace(key, value);
            }
            break;
        case kUpdateSet:
        default:
            for (const auto& [key, value] : update) {
                target[key] = value;
            }
            break;
    }
}

std::string make_unique_stream_tag(const std::string& base) {
    const uint64_t counter = g_pulse_stream_counter.fetch_add(1, std::memory_order_relaxed);
    std::ostringstream oss;
    oss << base << "#" << std::hex << std::setw(6) << std::setfill('0') << counter;
    return oss.str();
}

std::string make_wildcard_tag(const std::string& base) {
    return base + "*";
}

inline uint32_t min_version(uint32_t client_version) {
    return std::min<uint32_t>(client_version, kPulseProtocolVersion);
}

inline int set_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return -1;
    }
    return 0;
}

inline std::string errno_string(int err) {
    return std::string(strerror(err));
}

void set_cloexec(int fd) {
    if (fd < 0) {
        return;
    }
    int flags = fcntl(fd, F_GETFD);
    if (flags < 0) {
        return;
    }
    fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

void close_fd_vector(std::vector<int>& fds) {
    for (int fd : fds) {
        if (fd >= 0) {
            ::close(fd);
        }
    }
    fds.clear();
}

std::vector<int> extract_fds_from_msg(msghdr& msg) {
    std::vector<int> result;
    for (cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
            continue;
        }
        size_t payload_bytes = static_cast<size_t>(cmsg->cmsg_len) - CMSG_LEN(0);
        size_t count = payload_bytes / sizeof(int);
        const int* fd_ptr = reinterpret_cast<const int*>(CMSG_DATA(cmsg));
        for (size_t i = 0; i < count; ++i) {
            int fd = fd_ptr[i];
            if (fd >= 0 && result.size() < kMaxAncillaryFds) {
                set_cloexec(fd);
                result.push_back(fd);
            } else if (fd >= 0) {
                ::close(fd);
            }
        }
    }
    return result;
}

struct BufferAttr {
    uint32_t maxlength = kDefaultMaxLength;
    uint32_t tlength = kDefaultBufferLength;
    uint32_t prebuf = kDefaultPrebuf;
    uint32_t minreq = kDefaultMinReq;
};

struct StreamConfig {
    uint32_t sink_index = kInvalidIndex;
    std::string sink_name;
    SampleSpec sample_spec{};
    ChannelMap channel_map{};
    BufferAttr buffer_attr{};
    CVolume volume{};
    std::unordered_map<std::string, std::string> proplist;
    uint32_t sync_id = 0;
};

std::array<uint8_t, 8> default_channel_positions() {
    return {
        0, 1, 2, 3, 4, 5, 8, 9 // FL, FR, FC, LFE, SL, SR, RL, RR
    };
}

std::pair<uint8_t, uint8_t> guess_channel_layout(const ChannelMap& map) {
    if (map.channels == 1) {
        return {kChannelLayoutMono, 0x00};
    }
    if (map.channels == 2) {
        return {kChannelLayoutStereo, 0x00};
    }
    return {0x00, 0x00};
}

std::vector<uint8_t> convert_float_chunk_to_s32(const std::vector<uint8_t>& chunk) {
    const size_t samples = chunk.size() / sizeof(float);
    std::vector<uint8_t> converted(chunk.size());
    const float* input = reinterpret_cast<const float*>(chunk.data());
    int32_t* output = reinterpret_cast<int32_t*>(converted.data());
    constexpr double kScale = 2147483647.0;
    for (size_t i = 0; i < samples; ++i) {
        double clamped = std::clamp(static_cast<double>(input[i]), -1.0, 1.0);
        output[i] = static_cast<int32_t>(clamped * kScale);
    }
    return converted;
}

} // namespace

struct PulseAudioReceiver::Impl {
    struct Connection;

    PulseReceiverConfig config;
    std::shared_ptr<NotificationQueue> notification_queue;
    TimeshiftManager* timeshift_manager = nullptr;
    ClockManager* clock_manager = nullptr;
    std::string logger_prefix;

    int tcp_listen_fd = -1;
    int unix_listen_fd = -1;
    std::string unix_socket_path;

    std::vector<std::unique_ptr<Connection>> connections;
    std::vector<std::string> seen_tags;
    std::unordered_set<std::string> known_tags;
    std::unordered_map<std::string, std::unordered_set<std::string>> wildcard_to_composites;
    mutable std::mutex tag_map_mutex;

    PulseAudioReceiver::StreamTagResolvedCallback stream_tag_resolved_cb;
    PulseAudioReceiver::StreamTagRemovedCallback stream_tag_removed_cb;

    std::vector<uint8_t> auth_cookie;
    bool debug_packets = false;

    bool initialize();
    void shutdown_all();

    void event_loop(std::atomic<bool>& stop_flag);

    void accept_connections(int listen_fd, bool is_unix);

    void remove_connection(std::size_t index);

    void log(const std::string& msg) const {
        LOG_CPP_INFO("%s %s", logger_prefix.c_str(), msg.c_str());
    }

    void log_warning(const std::string& msg) const {
        LOG_CPP_WARNING("%s %s", logger_prefix.c_str(), msg.c_str());
    }

    void log_error(const std::string& msg) const {
        LOG_CPP_ERROR("%s %s", logger_prefix.c_str(), msg.c_str());
    }

    void log_debug(const std::string& msg) const {
        if (debug_packets) {
            LOG_CPP_INFO("%s %s", logger_prefix.c_str(), msg.c_str());
        }
    }

    bool load_cookie();

    void note_tag_seen(const std::string& tag) {
        std::string clean_tag = tag;
        strip_nuls(clean_tag);
        if (known_tags.insert(clean_tag).second) {
            log_debug("Discovered Pulse wildcard '" + clean_tag + "'");
            seen_tags.push_back(clean_tag);
            if (notification_queue) {
                notification_queue->push(DeviceDiscoveryNotification{clean_tag, DeviceDirection::CAPTURE, true});
            }
        }
    }

    void note_tag_removed(const std::string& tag) {
        std::string clean_tag = tag;
        strip_nuls(clean_tag);
        if (known_tags.erase(clean_tag) > 0) {
            log_debug("Pulse wildcard removed '" + clean_tag + "'");
            if (notification_queue) {
                notification_queue->push(DeviceDiscoveryNotification{clean_tag, DeviceDirection::CAPTURE, false});
            }
        }
    }

    void register_tag_mapping(const std::string& wildcard, const std::string& composite) {
        {
            std::lock_guard<std::mutex> lock(tag_map_mutex);
            wildcard_to_composites[wildcard].insert(composite);
        }
        log_debug("Registered Pulse wildcard '" + wildcard + "' -> '" + composite + "'");
        auto cb = stream_tag_resolved_cb;
        if (cb) {
            cb(wildcard, composite);
        }
    }

    void unregister_tag_mapping(const std::string& wildcard, const std::string& composite) {
        bool removed = false;
        {
            std::lock_guard<std::mutex> lock(tag_map_mutex);
            auto it = wildcard_to_composites.find(wildcard);
            if (it != wildcard_to_composites.end()) {
                it->second.erase(composite);
                if (it->second.empty()) {
                    wildcard_to_composites.erase(it);
                }
                removed = true;
            }
        }
        if (removed) {
            log_debug("Removed Pulse wildcard mapping for '" + wildcard + "' -> '" + composite + "'");
            auto cb = stream_tag_removed_cb;
            if (cb) {
                cb(wildcard);
            }
        }
    }

    std::vector<std::string> list_streams_for_wildcard(const std::string& wildcard) const {
        std::lock_guard<std::mutex> lock(tag_map_mutex);
        std::vector<std::string> streams;
        auto it = wildcard_to_composites.find(wildcard);
        if (it == wildcard_to_composites.end()) {
            return streams;
        }
        streams.insert(streams.end(), it->second.begin(), it->second.end());
        return streams;
    }

    std::optional<std::string> resolve_stream_tag_internal(const std::string& tag) const {
        if (tag.empty()) {
            return std::nullopt;
        }
        if (tag.back() != '*') {
            return tag;
        }
        std::lock_guard<std::mutex> lock(tag_map_mutex);
        auto it = wildcard_to_composites.find(tag);
        if (it == wildcard_to_composites.end() || it->second.empty()) {
            log_debug("No mapping for wildcard '" + tag + "'");
            return std::nullopt;
        }
        log_debug("Resolved wildcard '" + tag + "' -> '" + *it->second.begin() + "'");
        return *it->second.begin();
    }
};

struct PulseAudioReceiver::Impl::Connection {
    Impl* owner = nullptr;
    int fd = -1;
    bool is_unix = false;
    std::string peer_identity;
    std::string base_identity;
    std::string client_app_name;
    std::string client_process_binary;
    std::unordered_map<std::string, std::string> client_props;

    bool authorized = false;
    bool client_named = false;
    uint32_t negotiated_version = 13;

    std::vector<uint8_t> read_buffer;
    std::deque<std::vector<uint8_t>> write_queue;
    mutable std::mutex stream_mutex;

    struct ProfilingData {
        uint64_t chunks = 0;
        uint64_t chunk_bytes = 0;
        uint64_t frames = 0;
        uint64_t requests = 0;
        uint64_t request_bytes = 0;
        uint64_t catchup_events = 0;
        uint64_t catchup_usec = 0;
        uint64_t memfd_chunks = 0;
        uint64_t tcp_chunks = 0;
        uint64_t converted_chunks = 0;
        uint64_t latency_queries = 0;
        std::chrono::steady_clock::time_point window_start{};
        std::chrono::steady_clock::time_point last_log{};
    };

    struct PendingChunk {
        std::vector<uint8_t> audio_data;
        uint64_t start_frame = 0;
        size_t chunk_bytes = 0;
        uint64_t chunk_frames = 0;
        bool from_memfd = false;
        bool converted = false;
        uint64_t catchup_usec = 0;
        std::chrono::steady_clock::time_point play_time{};
    };

    struct StreamState {
        uint32_t local_index = 0;
        uint32_t sink_input_index = 0;
        BufferAttr buffer_attr;
        SampleSpec sample_spec;
        ChannelMap channel_map;
        CVolume volume;
        std::string composite_tag;
        std::string base_tag;
        std::string wildcard_tag;
        std::unordered_map<std::string, std::string> proplist;
        bool corked = false;
        uint32_t pending_request_bytes = 0;
        std::chrono::steady_clock::time_point next_request_time{};
        uint64_t frame_cursor = 0;
        ::screamrouter::audio::utils::ByteRingBuffer pending_payload;
        std::chrono::steady_clock::time_point last_delivery_time{};
        bool has_last_delivery = false;
        uint8_t chlayout1 = 0;
        uint8_t chlayout2 = 0;
        bool adjust_latency = false;
        bool early_requests = false;
        bool started_notified = false;
        std::string stream_name;
        bool playback_started = false;
        std::chrono::steady_clock::time_point playback_start_time{};
        uint64_t underrun_usec = 0;
        ProfilingData profile;
        std::deque<PendingChunk> pending_chunks;
        ClockManager::ConditionHandle clock_handle;
        uint64_t clock_last_sequence = 0;
        uint32_t samples_per_chunk = 0;
        // Extended RTP timeline state (audio clock units)
        // rtp_base is a randomized 32-bit offset to align with RTP best practices.
        // next_rtp_frame holds the next absolute 64-bit timestamp in RTP units
        // to ensure wrap-safe progression.
        uint64_t rtp_base = 0;
        uint64_t next_rtp_frame = 0;
        bool has_rtp_frame = false;
    };

    struct MemfdPool {
        int fd = -1;
        off_t size = 0;
    };

    std::unordered_map<uint32_t, StreamState> streams;
    uint32_t next_stream_index = 1;
    uint32_t next_sink_input_index = 1;
    uint32_t subscription_mask = 0;
    std::unordered_map<uint32_t, MemfdPool> memfd_pools;
    std::deque<std::vector<int>> pending_fds;
    bool use_shm = false;
    bool use_memfd = false;
    bool non_registered_memfd_error_logged = false;

    explicit Connection(Impl* impl, int socket_fd, bool unix_socket)
        : owner(impl), fd(socket_fd), is_unix(unix_socket) {
        read_buffer.reserve(4096);
    }

    ~Connection() {
        std::unordered_set<std::string> tags_to_reset;
        std::vector<std::pair<std::string, std::string>> wildcard_pairs;
        for (const auto& [_, stream] : streams) {
            wildcard_pairs.emplace_back(stream.wildcard_tag, stream.composite_tag);
            if (owner && owner->timeshift_manager) {
                tags_to_reset.insert(stream.composite_tag);
            }
        }

        for (auto& [_, stream] : streams) {
            unregister_stream_clock(stream);
        }
        streams.clear();

        if (owner && owner->timeshift_manager) {
            for (const auto& tag : tags_to_reset) {
                owner->timeshift_manager->reset_stream_state(tag);
            }
        }
        if (owner) {
            for (const auto& [wildcard, concrete] : wildcard_pairs) {
                owner->unregister_tag_mapping(wildcard, concrete);
                owner->note_tag_removed(wildcard);
            }
        }

        for (auto& entry : memfd_pools) {
            if (entry.second.fd >= 0) {
                ::close(entry.second.fd);
            }
        }
        memfd_pools.clear();
        for (auto& fds : pending_fds) {
            close_fd_vector(fds);
        }
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }

    bool handle_io(short revents);
    bool handle_read();
    bool handle_write();

    bool process_message(Message& message);
    void handle_clock_tick(uint32_t stream_index);
    void unregister_stream_clock(StreamState& stream);
    void register_stream_clock(StreamState& stream);
    void dispatch_clock_ticks();
    uint32_t calculate_samples_per_chunk(const StreamState& stream) const;
    bool handle_command(Command command, uint32_t tag, const uint8_t* payload, size_t length, std::vector<int>& fds);
    bool handle_auth(uint32_t tag, TagReader& reader);
    bool handle_set_client_name(uint32_t tag, TagReader& reader);
    bool handle_get_server_info(uint32_t tag);
    bool handle_subscribe(uint32_t tag, TagReader& reader);
    bool handle_lookup_sink(uint32_t tag, TagReader& reader);
    bool handle_get_sink_info(uint32_t tag, TagReader& reader, bool list);
    bool handle_get_card_info(uint32_t tag, TagReader& reader, bool list);
    bool handle_create_playback_stream(uint32_t tag, TagReader& reader);
    bool handle_delete_stream(uint32_t tag, TagReader& reader);
    bool handle_cork_stream(uint32_t tag, TagReader& reader);
    bool handle_flush_stream(uint32_t tag, TagReader& reader);
    bool handle_drain_stream(uint32_t tag, TagReader& reader);
    bool handle_set_buffer_attr(uint32_t tag, TagReader& reader);
    bool handle_get_playback_latency(uint32_t tag, TagReader& reader);
    bool handle_set_sink_input_volume(uint32_t tag, TagReader& reader);
    bool handle_set_stream_name(uint32_t tag, TagReader& reader);
    bool handle_update_playback_stream_proplist(uint32_t tag, TagReader& reader);
    bool handle_update_client_proplist(uint32_t tag, TagReader& reader);
    bool handle_register_memfd(uint32_t tag, TagReader& reader, std::vector<int>& fds);

    bool handle_playback_data(const Message& message);

    std::string composite_tag_for_stream(const std::unordered_map<std::string, std::string>& proplist) const;

    void enqueue_tagstruct(const TagWriter& writer);
    void enqueue_simple_reply(uint32_t tag);
    void enqueue_error(uint32_t tag, uint32_t error_code);
    void enqueue_request(uint32_t stream_index, uint32_t bytes);
    void enqueue_shm_release(uint32_t block_id);
    void enqueue_started(uint32_t stream_index);

    bool ensure_authorized(uint32_t tag);
    bool sample_format_supported(const SampleSpec& spec) const;
    uint32_t sample_format_bit_depth(uint8_t format) const;
    uint32_t effective_request_bytes(const StreamState& stream) const;
    void process_due_requests();
    std::optional<std::chrono::steady_clock::time_point> next_due_request() const;
    void record_chunk_metrics(StreamState& stream,
                              size_t chunk_bytes,
                              uint64_t frames,
                              bool from_memfd,
                              bool converted,
                              uint64_t catchup_usec,
                              std::chrono::steady_clock::time_point now);
    void record_request_metrics(uint32_t stream_index, uint32_t bytes);
    void record_latency_query(StreamState& stream);
    void maybe_log_stream_profile(uint32_t stream_index,
                                  StreamState& stream,
                                  std::chrono::steady_clock::time_point now);

    short desired_poll_events() const {
        short events = POLLIN;
        if (!write_queue.empty()) {
            events |= POLLOUT;
        }
        return events;
    }
};

bool PulseAudioReceiver::Impl::load_cookie() {
    auth_cookie.clear();
    if (!config.require_auth_cookie) {
        return true;
    }
    if (config.auth_cookie_path.empty()) {
        log_error("Auth cookie required but no cookie path specified");
        return false;
    }
    FILE* f = std::fopen(config.auth_cookie_path.c_str(), "rb");
    if (!f) {
        log_error("Failed to open auth cookie: " + std::string(std::strerror(errno)));
        return false;
    }
    auth_cookie.resize(kPulseCookieLength);
    size_t read = std::fread(auth_cookie.data(), 1, kPulseCookieLength, f);
    std::fclose(f);
    if (read != kPulseCookieLength) {
        log_error("Auth cookie file must be exactly 256 bytes");
        auth_cookie.clear();
        return false;
    }
    return true;
}

bool PulseAudioReceiver::Impl::initialize() {
    if (!load_cookie()) {
        return false;
    }

    debug_packets = true;
    log("PulseAudioReceiver protocol tracing enabled");

    // Setup TCP listener if requested.
    if (config.tcp_listen_port != 0) {
        tcp_listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (tcp_listen_fd < 0) {
            log_error("Failed to create TCP socket: " + errno_string(errno));
            return false;
        }

        int opt = 1;
        ::setsockopt(tcp_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        opt = 10000 * 15;
        ::setsockopt(tcp_listen_fd, SOL_SOCKET, SO_RCVBUF, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(config.tcp_listen_port);
        if (::bind(tcp_listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            log_error("Failed to bind TCP socket: " + errno_string(errno));
            ::close(tcp_listen_fd);
            tcp_listen_fd = -1;
            return false;
        }
        if (::listen(tcp_listen_fd, static_cast<int>(kMaxConnections)) < 0) {
            log_error("Failed to listen on TCP socket: " + errno_string(errno));
            ::close(tcp_listen_fd);
            tcp_listen_fd = -1;
            return false;
        }
        set_non_blocking(tcp_listen_fd);
        log("Listening for PulseAudio TCP clients on port " + std::to_string(config.tcp_listen_port));
    }

    if (!config.unix_socket_path.empty()) {
        unix_socket_path = config.unix_socket_path + "/native";
        std::string unix_pid_path = config.unix_socket_path + "/pid";
        FILE* pidf = fopen(unix_pid_path.c_str(), "w");
        if (pidf) {
            fprintf(pidf, "%d\n", getpid());
            fclose(pidf);
        } else {
            log_warning("Failed to write PID file: " + errno_string(errno));
        }
        ::unlink(unix_socket_path.c_str());

        unix_listen_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (unix_listen_fd < 0) {
            log_error("Failed to create UNIX socket: " + errno_string(errno));
            return false;
        }

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", unix_socket_path.c_str());
        if (::bind(unix_listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            log_error("Failed to bind UNIX socket: " + errno_string(errno));
            ::close(unix_listen_fd);
            unix_listen_fd = -1;
            return false;
        }
        int opt = 1152 * 10;
        ::setsockopt(unix_listen_fd, SOL_SOCKET, SO_RCVBUF, &opt, sizeof(opt));
        if (::listen(unix_listen_fd, static_cast<int>(kMaxConnections)) < 0) {
            log_error("Failed to listen on UNIX socket: " + errno_string(errno));
            ::close(unix_listen_fd);
            unix_listen_fd = -1;
            return false;
        }

        if (!config.socket_owner_user.empty() || !config.socket_owner_group.empty()) {
            uid_t uid = static_cast<uid_t>(-1);
            gid_t gid = static_cast<gid_t>(-1);
            if (!config.socket_owner_user.empty()) {
                struct passwd* pw = getpwnam(config.socket_owner_user.c_str());
                if (pw) uid = pw->pw_uid;
            }
            if (!config.socket_owner_group.empty()) {
                struct group* gr = getgrnam(config.socket_owner_group.c_str());
                if (gr) gid = gr->gr_gid;
            }
            if (::chown(unix_socket_path.c_str(), uid, gid) < 0) {
                log_warning("Failed to chown UNIX socket: " + errno_string(errno));
            }
        }
        ::chmod(unix_socket_path.c_str(), static_cast<mode_t>(config.socket_permissions));
        set_non_blocking(unix_listen_fd);
        log("Listening for PulseAudio UNIX clients on " + unix_socket_path);
    }

    if (tcp_listen_fd < 0 && unix_listen_fd < 0) {
        log_error("PulseAudio receiver requires at least one transport (TCP or UNIX)");
        return false;
    }

    return true;
}

void PulseAudioReceiver::Impl::shutdown_all() {
    connections.clear();

    if (tcp_listen_fd >= 0) {
        ::close(tcp_listen_fd);
        tcp_listen_fd = -1;
    }
    if (unix_listen_fd >= 0) {
        ::close(unix_listen_fd);
        unix_listen_fd = -1;
    }
    if (!unix_socket_path.empty()) {
        ::unlink(unix_socket_path.c_str());
    }
}

void PulseAudioReceiver::Impl::accept_connections(int listen_fd, bool is_unix) {
    while (true) {
        sockaddr_storage ss{};
        socklen_t len = sizeof(ss);
        int client_fd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&ss), &len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            log_warning("accept failed: " + errno_string(errno));
            break;
        }

        if (connections.size() >= kMaxConnections) {
            log_warning("Too many PulseAudio clients; rejecting connection");
            ::close(client_fd);
            continue;
        }

        set_non_blocking(client_fd);
        auto conn = std::make_unique<Connection>(this, client_fd, is_unix);

        if (!is_unix) {
            char host[NI_MAXHOST] = {0};
            char serv[NI_MAXSERV] = {0};
            const int gi = getnameinfo(reinterpret_cast<const sockaddr*>(&ss), len,
                                       host, sizeof(host),
                                       serv, sizeof(serv),
                                       NI_NUMERICHOST | NI_NUMERICSERV);
            if (gi == 0 && host[0] != '\0') {
                conn->peer_identity = std::string(host) + ":" + serv;
            } else {
                if (ss.ss_family == AF_INET) {
                    auto* sin = reinterpret_cast<sockaddr_in*>(&ss);
                    inet_ntop(AF_INET, &sin->sin_addr, host, sizeof(host));
                } else if (ss.ss_family == AF_INET6) {
                    auto* sin6 = reinterpret_cast<sockaddr_in6*>(&ss);
                    inet_ntop(AF_INET6, &sin6->sin6_addr, host, sizeof(host));
                }
                conn->peer_identity = host[0] ? std::string(host) : "unknown";
            }
        } else {
#ifdef SO_PEERCRED
            ucred cred{};
            socklen_t cl = sizeof(cred);
            if (::getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &cred, &cl) == 0) {
                (void)cred;
                conn->peer_identity = "127.0.0.1";
            }
#endif
            if (conn->peer_identity.empty()) {
                conn->peer_identity = "local";
            }
        }

        conn->base_identity = conn->peer_identity;

        log("Accepted PulseAudio client from " + conn->peer_identity);
        connections.push_back(std::move(conn));
    }
}

void PulseAudioReceiver::Impl::remove_connection(std::size_t index) {
    if (index >= connections.size()) {
        return;
    }
    log("Closing PulseAudio client " + connections[index]->peer_identity);
    connections.erase(connections.begin() + static_cast<long>(index));
}

bool PulseAudioReceiver::Impl::Connection::handle_io(short revents) {
    if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
        return false;
    }
    if ((revents & POLLIN) && !handle_read()) {
        return false;
    }
    if ((revents & POLLOUT) && !handle_write()) {
        return false;
    }
    return true;
}

bool PulseAudioReceiver::Impl::Connection::handle_read() {
    std::array<uint8_t, 4096> buffer{};
    while (true) {
        struct iovec iov {
            buffer.data(),
            buffer.size()
        };
        char control[CMSG_SPACE(sizeof(int) * kMaxAncillaryFds)]{};
        struct msghdr msg {};
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = control;
        msg.msg_controllen = sizeof(control);

        int recv_flags = 0;
#ifdef MSG_CMSG_CLOEXEC
        recv_flags |= MSG_CMSG_CLOEXEC;
#endif
        ssize_t r = ::recvmsg(fd, &msg, recv_flags);
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            owner->log_warning("recvmsg failed: " + errno_string(errno));
            return false;
        }
        if (r == 0) {
            return false; // peer closed
        }

        if ((msg.msg_flags & MSG_TRUNC) != 0 || (msg.msg_flags & MSG_CTRUNC) != 0) {
            owner->log_warning("Ancillary data truncated while receiving PulseAudio frame");
        }

        read_buffer.insert(read_buffer.end(), buffer.begin(), buffer.begin() + r);

        auto fds = extract_fds_from_msg(msg);
        if (!fds.empty()) {
            pending_fds.push_back(std::move(fds));
        }
    }

    while (true) {
        Message message;
        size_t consumed = DecodeMessage(read_buffer.data(), read_buffer.size(), message);
        if (consumed == 0) {
            break; // need more data
        }
        read_buffer.erase(read_buffer.begin(), read_buffer.begin() + static_cast<long>(consumed));

        if (!pending_fds.empty()) {
            message.fds = std::move(pending_fds.front());
            pending_fds.pop_front();
        }

        if (!process_message(message)) {
            close_fd_vector(message.fds);
            return false;
        }
        close_fd_vector(message.fds);
    }
    return true;
}

bool PulseAudioReceiver::Impl::Connection::handle_write() {
    while (!write_queue.empty()) {
        auto& frame = write_queue.front();
        ssize_t written = ::send(fd, frame.data(), frame.size(), 0);
        if (written < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true;
            }
            owner->log_warning("send failed: " + errno_string(errno));
            return false;
        }
        if (static_cast<size_t>(written) < frame.size()) {
            frame.erase(frame.begin(), frame.begin() + written);
            return true;
        }
        write_queue.pop_front();
    }
    return true;
}

void PulseAudioReceiver::Impl::Connection::enqueue_tagstruct(const TagWriter& writer) {
    Message message;
    message.descriptor.length = static_cast<uint32_t>(writer.buffer().size());
    message.descriptor.channel = kChannelCommand;
    message.payload = writer.buffer();
    if (owner->debug_packets) {
        owner->log_debug("SEND cmd frame len=" + std::to_string(message.payload.size()));
    }
    write_queue.push_back(EncodeMessage(message));
}

void PulseAudioReceiver::Impl::Connection::enqueue_simple_reply(uint32_t tag) {
    TagWriter writer;
    writer.put_command(Command::Reply, tag);
    enqueue_tagstruct(writer);
}

void PulseAudioReceiver::Impl::Connection::enqueue_error(uint32_t tag, uint32_t error_code) {
    TagWriter writer;
    writer.put_command(Command::Error, tag);
    writer.put_u32(error_code);
    enqueue_tagstruct(writer);
}

void PulseAudioReceiver::Impl::Connection::enqueue_request(uint32_t stream_index, uint32_t bytes) {
    if (bytes == 0) {
        return;
    }
    record_request_metrics(stream_index, bytes);
    TagWriter writer;
    writer.put_command(Command::Request, static_cast<uint32_t>(-1));
    writer.put_u32(stream_index);
    writer.put_u32(bytes);
    enqueue_tagstruct(writer);
}

void PulseAudioReceiver::Impl::Connection::record_chunk_metrics(StreamState& stream,
                                                                size_t chunk_bytes,
                                                                uint64_t frames,
                                                                bool from_memfd,
                                                                bool converted,
                                                                uint64_t catchup_usec,
                                                                std::chrono::steady_clock::time_point now) {
    if (!owner->debug_packets) {
        return;
    }
    auto& profile = stream.profile;
    if (profile.window_start == std::chrono::steady_clock::time_point{}) {
        profile.window_start = now;
        profile.last_log = now;
    }
    profile.chunks += 1;
    profile.chunk_bytes += chunk_bytes;
    profile.frames += frames;
    if (from_memfd) {
        profile.memfd_chunks += 1;
    } else {
        profile.tcp_chunks += 1;
    }
    if (converted) {
        profile.converted_chunks += 1;
    }
    if (catchup_usec > 0) {
        profile.catchup_events += 1;
        profile.catchup_usec += catchup_usec;
    }
}

void PulseAudioReceiver::Impl::Connection::record_request_metrics(uint32_t stream_index, uint32_t bytes) {
    if (!owner->debug_packets) {
        return;
    }
    auto it = streams.find(stream_index);
    if (it == streams.end()) {
        return;
    }
    auto& profile = it->second.profile;
    auto now = std::chrono::steady_clock::now();
    if (profile.window_start == std::chrono::steady_clock::time_point{}) {
        profile.window_start = now;
        profile.last_log = now;
    }
    profile.requests += 1;
    profile.request_bytes += bytes;
}

void PulseAudioReceiver::Impl::Connection::record_latency_query(StreamState& stream) {
    if (!owner->debug_packets) {
        return;
    }
    auto now = std::chrono::steady_clock::now();
    auto& profile = stream.profile;
    if (profile.window_start == std::chrono::steady_clock::time_point{}) {
        profile.window_start = now;
        profile.last_log = now;
    }
    profile.latency_queries += 1;
}

void PulseAudioReceiver::Impl::Connection::maybe_log_stream_profile(uint32_t stream_index,
                                                                    StreamState& stream,
                                                                    std::chrono::steady_clock::time_point now) {
    if (!owner->debug_packets) {
        return;
    }
    auto& profile = stream.profile;
    if (profile.window_start == std::chrono::steady_clock::time_point{}) {
        return;
    }
    if (profile.chunks == 0 && profile.requests == 0 && profile.catchup_events == 0 && profile.latency_queries == 0) {
        profile.window_start = now;
        profile.last_log = now;
        return;
    }
    const auto elapsed = now - profile.window_start;
    if (elapsed < std::chrono::seconds(5) && profile.catchup_events < 3) {
        return;
    }
    if (profile.catchup_events >= 3 && elapsed < std::chrono::milliseconds(200)) {
        return;
    }

    double seconds = std::chrono::duration<double>(elapsed).count();
    if (seconds <= 0.0) {
        seconds = 1.0;
    }

    const double chunk_rate = static_cast<double>(profile.chunks) / seconds;
    const double frame_rate = static_cast<double>(profile.frames) / seconds;
    const double request_rate = static_cast<double>(profile.request_bytes) / seconds;

    std::ostringstream oss;
    oss << "Profile stream=" << stream_index
        << " chunks=" << profile.chunks
        << " bytes=" << profile.chunk_bytes
        << " frames=" << profile.frames
        << " reqs=" << profile.requests
        << " req_bytes=" << profile.request_bytes
        << " chunk_rate=" << std::fixed << std::setprecision(2) << chunk_rate << "/s"
        << " frame_rate=" << std::fixed << std::setprecision(2) << frame_rate << "/s"
        << " req_rate=" << std::fixed << std::setprecision(2) << request_rate << " B/s"
        << " catchup_events=" << profile.catchup_events
        << " catchup_usec=" << profile.catchup_usec
        << " latency_queries=" << profile.latency_queries
        << " memfd_chunks=" << profile.memfd_chunks
        << " tcp_chunks=" << profile.tcp_chunks
        << " converted_chunks=" << profile.converted_chunks
        << " underrun_total=" << stream.underrun_usec;

    owner->log_debug(oss.str());

    profile.chunks = 0;
    profile.chunk_bytes = 0;
    profile.frames = 0;
    profile.requests = 0;
    profile.request_bytes = 0;
    profile.catchup_events = 0;
    profile.catchup_usec = 0;
    profile.memfd_chunks = 0;
    profile.tcp_chunks = 0;
    profile.converted_chunks = 0;
    profile.latency_queries = 0;
    profile.window_start = now;
    profile.last_log = now;
}

uint32_t PulseAudioReceiver::Impl::Connection::calculate_samples_per_chunk(const StreamState& stream) const {
    const uint32_t bit_depth = sample_format_bit_depth(stream.sample_spec.format);
    if (bit_depth == 0 || stream.sample_spec.channels == 0) {
        return 0;
    }
    const uint32_t frame_bytes = (bit_depth / 8u) * static_cast<uint32_t>(stream.sample_spec.channels);
    if (frame_bytes == 0 || (CHUNK_SIZE % frame_bytes) != 0) {
        return 0;
    }
    return CHUNK_SIZE / frame_bytes;
}

void PulseAudioReceiver::Impl::Connection::register_stream_clock(StreamState& stream) {
    if (!owner->clock_manager || stream.clock_handle.valid()) {
        return;
    }
    const uint32_t bit_depth = sample_format_bit_depth(stream.sample_spec.format);
    if (bit_depth == 0 || stream.sample_spec.rate == 0 || stream.sample_spec.channels == 0) {
        return;
    }
    try {
        stream.clock_handle = owner->clock_manager->register_clock_condition(
            static_cast<int>(stream.sample_spec.rate),
            static_cast<int>(stream.sample_spec.channels),
            static_cast<int>(bit_depth));
        if (!stream.clock_handle.valid()) {
            throw std::runtime_error("ClockManager returned invalid condition handle");
        }
        if (auto condition = stream.clock_handle.condition) {
            std::lock_guard<std::mutex> condition_lock(condition->mutex);
            stream.clock_last_sequence = condition->sequence;
        } else {
            stream.clock_last_sequence = 0;
        }
    } catch (const std::exception& ex) {
        owner->log_error("Failed to register PulseAudio stream clock for " + stream.composite_tag + ": " + ex.what());
    }
}

void PulseAudioReceiver::Impl::Connection::unregister_stream_clock(StreamState& stream) {
    if (!owner->clock_manager || !stream.clock_handle.valid()) {
        return;
    }
    try {
        owner->clock_manager->unregister_clock_condition(stream.clock_handle);
    } catch (const std::exception& ex) {
        owner->log_error("Failed to unregister PulseAudio stream clock for " + stream.composite_tag + ": " + ex.what());
    }
    stream.clock_handle = {};
    stream.clock_last_sequence = 0;
}

void PulseAudioReceiver::Impl::Connection::dispatch_clock_ticks() {
    if (!owner->clock_manager) {
        return;
    }

    std::vector<std::pair<uint32_t, uint64_t>> pending_ticks;

    {
        std::lock_guard<std::mutex> lock(stream_mutex);
        for (auto& [stream_index, stream] : streams) {
            if (!stream.clock_handle.valid()) {
                continue;
            }

            auto condition = stream.clock_handle.condition;
            if (!condition) {
                continue;
            }

            uint64_t sequence_snapshot = 0;
            {
                std::unique_lock<std::mutex> condition_lock(condition->mutex);
                sequence_snapshot = condition->sequence;
            }

            if (sequence_snapshot > stream.clock_last_sequence) {
                uint64_t tick_count = sequence_snapshot - stream.clock_last_sequence;
                stream.clock_last_sequence = sequence_snapshot;
                pending_ticks.emplace_back(stream_index, tick_count);
            }
        }
    }

    for (const auto& [stream_index, tick_count] : pending_ticks) {
        for (uint64_t i = 0; i < tick_count; ++i) {
            if (owner->clock_manager == nullptr) {
                return;
            }
            handle_clock_tick(stream_index);
        }
    }
}

void PulseAudioReceiver::Impl::Connection::handle_clock_tick(uint32_t stream_index) {
    TaggedAudioPacket packet;
    bool should_send = false;
    {
        std::lock_guard<std::mutex> lock(stream_mutex);
        auto it = streams.find(stream_index);
        if (it == streams.end()) {
            return;
        }
        auto& stream = it->second;
        const uint32_t bit_depth = sample_format_bit_depth(stream.sample_spec.format);
        if (bit_depth == 0) {
            return;
        }
        if (stream.samples_per_chunk == 0) {
            stream.samples_per_chunk = calculate_samples_per_chunk(stream);
            if (stream.samples_per_chunk == 0) {
                owner->log_error("Unsupported PulseAudio format for clock scheduling on stream " + stream.composite_tag);
                return;
            }
        }

        auto now = std::chrono::steady_clock::now();
        if (!stream.pending_chunks.empty()) {
            auto pending = std::move(stream.pending_chunks.front());
            stream.pending_chunks.pop_front();

            record_chunk_metrics(stream,
                                 pending.chunk_bytes,
                                 pending.chunk_frames,
                                 pending.from_memfd,
                                 pending.converted,
                                 pending.catchup_usec,
                                 pending.play_time);

            packet.audio_data = std::move(pending.audio_data);
            auto now_stamped = std::chrono::steady_clock::now();
            if (pending.play_time.time_since_epoch().count() == 0) {
                packet.received_time = now_stamped;
            } else {
                packet.received_time = pending.play_time;
            }
            // Map chunk start frame to RTP timeline using randomized base
            uint64_t start_abs = stream.rtp_base + pending.start_frame;
            packet.rtp_timestamp = static_cast<uint32_t>(start_abs & 0xFFFFFFFFu);
            stream.next_rtp_frame = start_abs + pending.chunk_frames;
            stream.has_rtp_frame = true;
        } else {
            record_chunk_metrics(stream,
                                 CHUNK_SIZE,
                                 stream.samples_per_chunk,
                                 false,
                                 false,
                                 0,
                                 now);
            packet.audio_data.assign(CHUNK_SIZE, 0);
            packet.received_time = std::chrono::steady_clock::now();
            if (!stream.has_rtp_frame) {
                stream.has_rtp_frame = true;
            }
            packet.rtp_timestamp = static_cast<uint32_t>(stream.next_rtp_frame & 0xFFFFFFFFu);
            stream.next_rtp_frame += stream.samples_per_chunk;
        }

        packet.source_tag = stream.composite_tag;
        packet.sample_rate = stream.sample_spec.rate;
        packet.channels = stream.sample_spec.channels;
        packet.bit_depth = bit_depth;
        packet.chlayout1 = stream.chlayout1;
        packet.chlayout2 = stream.chlayout2;
        packet.playback_rate = 1.0;

        should_send = true;
    }

    if (should_send && owner->timeshift_manager) {
        owner->timeshift_manager->add_packet(std::move(packet));
    }
}


void PulseAudioReceiver::Impl::Connection::enqueue_shm_release(uint32_t block_id) {
    Message message;
    message.descriptor.length = 0;
    message.descriptor.channel = static_cast<uint32_t>(-1);
    message.descriptor.offset_hi = block_id;
    message.descriptor.offset_lo = 0;
    message.descriptor.flags = kDescriptorFlagShmRelease;
    write_queue.push_back(EncodeMessage(message));
}

void PulseAudioReceiver::Impl::Connection::enqueue_started(uint32_t stream_index) {
    TagWriter writer;
    writer.put_command(Command::Started, static_cast<uint32_t>(-1));
    writer.put_u32(stream_index);
    enqueue_tagstruct(writer);
}

bool PulseAudioReceiver::Impl::Connection::process_message(Message& message) {
    if (message.descriptor.channel == kChannelCommand) {
        TagReader header_reader(message.payload.data(), message.payload.size());
        auto command_field = header_reader.read_u32();
        auto tag_field = header_reader.read_u32();
        if (!command_field || !tag_field) {
            owner->log_warning("Received malformed command frame");
            return false;
        }
        Command command = static_cast<Command>(*command_field);
        uint32_t tag = *tag_field;

        if (header_reader.bytes_consumed() > message.payload.size()) {
            owner->log_warning("Command header overran payload");
            return false;
        }

        const uint8_t* payload_ptr = header_reader.current_data();
        size_t payload_length = message.payload.size() - header_reader.bytes_consumed();

        TagReader reader(payload_ptr, payload_length);
        if (owner->debug_packets) {
            std::ostringstream oss;
            oss << "RECV cmd=" << command_name(command)
                << " tag=" << tag
                << " payload=" << std::hex;
            for (size_t i = 0; i < message.payload.size(); ++i) {
                oss << std::setw(2) << std::setfill('0')
                    << static_cast<int>(message.payload[i]);
            }
            owner->log_debug(oss.str());
        }
        return handle_command(command, tag, payload_ptr, payload_length, message.fds);
    }
    if (owner->debug_packets) {
        owner->log_debug("RECV playback frame stream=" + std::to_string(message.descriptor.channel) +
                         " payload=" + std::to_string(message.payload.size()));
    }
    return handle_playback_data(message);
}

bool PulseAudioReceiver::Impl::Connection::ensure_authorized(uint32_t tag) {
    if (!authorized) {
        enqueue_error(tag, PA_ERR_ACCESS);
        return false;
    }
    return true;
}

bool PulseAudioReceiver::Impl::Connection::sample_format_supported(const SampleSpec& spec) const {
    if (spec.channels == 0 || spec.channels > 8) {
        return false;
    }
    if (spec.rate == 0) {
        return false;
    }
    switch (spec.format) {
        case kSampleFormatS16LE:
        case kSampleFormatS32LE:
        case kSampleFormatFloat32LE:
            return true;
        default:
            return false;
    }
}

uint32_t PulseAudioReceiver::Impl::Connection::sample_format_bit_depth(uint8_t format) const {
    switch (format) {
        case kSampleFormatS16LE:
            return 16;
        case kSampleFormatFloat32LE:
        case kSampleFormatS32LE:
            return 32;
        default:
            return 32;
    }
}

uint32_t PulseAudioReceiver::Impl::Connection::effective_request_bytes(const StreamState& stream) const {
    uint32_t request = stream.buffer_attr.minreq;
    if (request == 0 || request == static_cast<uint32_t>(-1)) {
        request = kDefaultMinReq;
    }
    if (stream.buffer_attr.tlength != 0 && stream.buffer_attr.tlength != static_cast<uint32_t>(-1)) {
        request = std::min(request, stream.buffer_attr.tlength);
    }
    return std::max<uint32_t>(request, kDefaultMinReq);
}

bool PulseAudioReceiver::Impl::Connection::handle_command(Command command,
                                                          uint32_t tag,
                                                          const uint8_t* payload,
                                                          size_t length,
                                                          std::vector<int>& fds) {
    TagReader reader(payload, length);

    owner->log("Cmd " + std::string(command_name(command)) + " tag=" + std::to_string(tag));

    switch (command) {
        case Command::Auth:
            return handle_auth(tag, reader);
        case Command::SetClientName:
            return handle_set_client_name(tag, reader);
        case Command::GetServerInfo:
            return handle_get_server_info(tag);
        case Command::Subscribe:
            return handle_subscribe(tag, reader);
        case Command::LookupSink:
            return handle_lookup_sink(tag, reader);
        case Command::GetSinkInfo:
            return handle_get_sink_info(tag, reader, false);
        case Command::GetSinkInfoList:
            return handle_get_sink_info(tag, reader, true);
        case Command::GetCardInfo:
            return handle_get_card_info(tag, reader, false);
        case Command::GetCardInfoList:
            return handle_get_card_info(tag, reader, true);
        case Command::CreatePlaybackStream:
            return handle_create_playback_stream(tag, reader);
        case Command::DeletePlaybackStream:
            return handle_delete_stream(tag, reader);
        case Command::CorkPlaybackStream:
            return handle_cork_stream(tag, reader);
        case Command::FlushPlaybackStream:
            return handle_flush_stream(tag, reader);
        case Command::DrainPlaybackStream:
            return handle_drain_stream(tag, reader);
        case Command::SetPlaybackStreamBufferAttr:
            return handle_set_buffer_attr(tag, reader);
        case Command::GetPlaybackLatency:
            return handle_get_playback_latency(tag, reader);
        case Command::SetSinkInputVolume:
            return handle_set_sink_input_volume(tag, reader);
        case Command::SetPlaybackStreamName:
            return handle_set_stream_name(tag, reader);
        case Command::UpdatePlaybackStreamProplist:
            return handle_update_playback_stream_proplist(tag, reader);
        case Command::UpdateClientProplist:
            return handle_update_client_proplist(tag, reader);
        case Command::RegisterMemfdShmid:
            return handle_register_memfd(tag, reader, fds);
        case Command::Exit:
            return false;
        default:
            owner->log_warning("Unsupported command " + std::string(command_name(command)));
            enqueue_error(tag, PA_ERR_NOTSUPPORTED);
            return true;
    }
}

bool PulseAudioReceiver::Impl::Connection::handle_auth(uint32_t tag, TagReader& reader) {
    auto version_field = reader.read_u32();
    if (!version_field) {
        owner->log_warning("AUTH missing version");
        enqueue_error(tag, PA_ERR_PROTOCOL);
        return false;
    }
    uint32_t client_version_word = *version_field;
    uint32_t client_version = client_version_word & kProtocolVersionMask;
    bool client_shm_supported = (client_version >= 13) && ((client_version_word & kProtocolFlagSHM) != 0);
    bool client_memfd_supported = (client_version >= 31) && ((client_version_word & kProtocolFlagMemFd) != 0);

    use_shm = client_shm_supported;
    use_memfd = use_shm && client_memfd_supported;
    non_registered_memfd_error_logged = false;

    auto cookie = reader.read_arbitrary();
    if (!cookie || cookie->size() != kPulseCookieLength) {
        owner->log_warning("AUTH missing cookie len=" + std::to_string(cookie ? cookie->size() : 0));
        enqueue_error(tag, PA_ERR_PROTOCOL);
        return false;
    }

    if (owner->config.require_auth_cookie) {
        if (owner->auth_cookie.empty() || *cookie != owner->auth_cookie) {
            owner->log_warning("AUTH cookie mismatch");
            enqueue_error(tag, PA_ERR_ACCESS);
            return false;
        }
    }

    negotiated_version = min_version(client_version);
    authorized = true;

    TagWriter writer;
    writer.put_command(Command::Reply, tag);
    uint32_t response_version = negotiated_version;
    if (use_shm) {
        response_version |= kProtocolFlagSHM;
    }
    if (use_memfd && negotiated_version >= 31) {
        response_version |= kProtocolFlagMemFd;
    } else {
        use_memfd = false;
    }
    writer.put_u32(response_version);
    owner->log("Auth OK, negotiated version " + std::to_string(negotiated_version));
    enqueue_tagstruct(writer);
    return true;
}

bool PulseAudioReceiver::Impl::Connection::handle_set_client_name(uint32_t tag, TagReader& reader) {
    if (!ensure_authorized(tag)) {
        return true;
    }

    client_props.clear();

    if (negotiated_version >= 13) {
        auto props = reader.read_proplist();
        if (!props) {
            enqueue_error(tag, PA_ERR_PROTOCOL);
            return false;
        }
        client_props = std::move(*props);
    } else {
        auto name = reader.read_string();
        if (!name) {
            enqueue_error(tag, PA_ERR_PROTOCOL);
            return false;
        }
        client_props.emplace("application.name", *name);
    }

    client_app_name = client_props.count("application.name") ? client_props["application.name"] : std::string();
    client_process_binary = client_props.count("application.process.binary") ? client_props["application.process.binary"] : std::string();

    client_named = true;
    TagWriter writer;
    writer.put_command(Command::Reply, tag);
    if (negotiated_version >= 13) {
        writer.put_u32(0); // pseudo client index
    }
    enqueue_tagstruct(writer);
    return true;
}

bool PulseAudioReceiver::Impl::Connection::handle_get_server_info(uint32_t tag) {
    if (!ensure_authorized(tag)) {
        return true;
    }

    TagWriter writer;
    writer.put_command(Command::Reply, tag);
    writer.put_string("ScreamRouter");
    writer.put_string("1.0");
    writer.put_string("screamrouter");
    writer.put_string("localhost");

    SampleSpec ss{};
    ss.format = kSampleFormatS32LE;
    ss.channels = 8;
    ss.rate = 48000;
    writer.put_sample_spec(ss);

    writer.put_string(kVirtualSinkName);
    writer.put_nullable_string(nullptr); // default source
    writer.put_u32(0);

    enqueue_tagstruct(writer);
    return true;
}

bool PulseAudioReceiver::Impl::Connection::handle_subscribe(uint32_t tag, TagReader& reader) {
    if (!ensure_authorized(tag)) {
        return true;
    }
    auto mask = reader.read_u32();
    if (!mask || !reader.eof()) {
        owner->log_warning("SetClientName proplist parse failed");
        enqueue_error(tag, PA_ERR_PROTOCOL);
        return false;
    }
    subscription_mask = *mask;
    enqueue_simple_reply(tag);
    return true;
}

bool PulseAudioReceiver::Impl::Connection::handle_lookup_sink(uint32_t tag, TagReader& reader) {
    if (!ensure_authorized(tag)) {
        return true;
    }
    auto name = reader.read_string();
    if (!name || !reader.eof()) {
        owner->log_warning("LookupSink payload parse failure");
        enqueue_error(tag, PA_ERR_PROTOCOL);
        return false;
    }
    if (!name->empty() && *name != kVirtualSinkName) {
        enqueue_error(tag, PA_ERR_NOENTITY);
        return true;
    }
    TagWriter writer;
    writer.put_command(Command::Reply, tag);
    writer.put_u32(kVirtualSinkIndex);
    enqueue_tagstruct(writer);
    return true;
}

bool PulseAudioReceiver::Impl::Connection::handle_get_sink_info(uint32_t tag, TagReader& reader, bool list) {
    if (!ensure_authorized(tag)) {
        return true;
    }

    if (list) {
        if (!reader.eof()) {
            enqueue_error(tag, PA_ERR_PROTOCOL);
            return false;
        }
    } else {
        auto index = reader.read_u32();
        auto name = reader.read_string();
        if (!index || !name || !reader.eof()) {
            enqueue_error(tag, PA_ERR_PROTOCOL);
            return false;
        }
        if (*index != kInvalidIndex && *index != kVirtualSinkIndex) {
            enqueue_error(tag, PA_ERR_NOENTITY);
            return true;
        }
        if (!name->empty() && *name != kVirtualSinkName) {
            enqueue_error(tag, PA_ERR_NOENTITY);
            return true;
        }
    }

    TagWriter writer;
    writer.put_command(Command::Reply, tag);
    writer.put_u32(kVirtualSinkIndex);
    writer.put_string(kVirtualSinkName);
    writer.put_string(kVirtualSinkDescription);

    SampleSpec ss{};
    ss.format = kSampleFormatS32LE;
    ss.channels = 8;
    ss.rate = 48000;
    writer.put_sample_spec(ss);

    ChannelMap map;
    map.channels = 8;
    auto positions = default_channel_positions();
    map.map.assign(positions.begin(), positions.begin() + map.channels);
    writer.put_channel_map(map);

    writer.put_u32(kInvalidIndex);

    CVolume vol;
    vol.channels = ss.channels;
    vol.values.assign(vol.channels, kVolumeNorm);
    writer.put_cvolume(vol);
    writer.put_bool(false);
    writer.put_u32(kInvalidIndex);
    writer.put_nullable_string(nullptr);
    writer.put_u64(0);
    writer.put_string(kVirtualSinkName);
    writer.put_u32(0);

    if (negotiated_version >= 13) {
        Proplist props;
        props["device.description"] = kVirtualSinkDescription;
        props["device.product.name"] = "ScreamRouter";
        writer.put_proplist(props);
        writer.put_u64(0);
    }

    enqueue_tagstruct(writer);
    return true;
}

bool PulseAudioReceiver::Impl::Connection::handle_get_card_info(uint32_t tag, TagReader& reader, bool list) {
    if (!ensure_authorized(tag)) {
        return true;
    }

    if (list) {
        if (!reader.eof()) {
            owner->log_warning("GetCardInfoList parse failure");
            enqueue_error(tag, PA_ERR_PROTOCOL);
            return false;
        }

        // We do not expose any card objects; respond with an empty list.
        TagWriter writer;
        writer.put_command(Command::Reply, tag);
        enqueue_tagstruct(writer);
        return true;
    }

    auto index = reader.read_u32();
    if (!index) {
        owner->log_warning("GetCardInfo parse failure (missing index)");
        enqueue_error(tag, PA_ERR_PROTOCOL);
        return false;
    }

    // Name string is optional depending on caller; consume if present.
    if (!reader.eof()) {
        auto name = reader.read_string();
        if (!name) {
            owner->log_warning("GetCardInfo parse failure (invalid name)");
            enqueue_error(tag, PA_ERR_PROTOCOL);
            return false;
        }
    }

    if (!reader.eof()) {
        owner->log_warning("GetCardInfo trailing payload");
        enqueue_error(tag, PA_ERR_PROTOCOL);
        return false;
    }

    // No card metadata is available; signal that the requested entity does not exist.
    enqueue_error(tag, PA_ERR_NOENTITY);
    return true;
}

bool PulseAudioReceiver::Impl::Connection::handle_create_playback_stream(uint32_t tag, TagReader& reader) {
    std::lock_guard<std::mutex> stream_lock(stream_mutex);
    if (!ensure_authorized(tag)) {
        return true;
    }

    StreamConfig config;

    auto sample_spec = reader.read_sample_spec();
    if (!sample_spec) {
        owner->log_warning("GetSinkInfo parse failure");
        enqueue_error(tag, PA_ERR_PROTOCOL);
        return false;
    }
    config.sample_spec = *sample_spec;
    owner->log("CreatePlaybackStream sample spec format=" + std::to_string(config.sample_spec.format) +
               " channels=" + std::to_string(config.sample_spec.channels) +
               " rate=" + std::to_string(config.sample_spec.rate));

    auto channel_map = reader.read_channel_map();
    if (!channel_map) {
        owner->log_warning("CreatePlaybackStream parse failure");
        enqueue_error(tag, PA_ERR_PROTOCOL);
        return false;
    }
    config.channel_map = *channel_map;

    auto sink_index = reader.read_u32();
    auto sink_name = reader.read_string();
    auto maxlength = reader.read_u32();
    auto corked = reader.read_bool();
    auto tlength = reader.read_u32();
    auto prebuf = reader.read_u32();
    auto minreq = reader.read_u32();
    auto sync_id = reader.read_u32();
    auto cvolume = reader.read_cvolume();

    if (!sink_index || !sink_name || !maxlength || !corked || !tlength || !prebuf || !minreq || !sync_id || !cvolume) {
        owner->log_warning("CreatePlaybackStream extended parse failure");
        enqueue_error(tag, PA_ERR_PROTOCOL);
        return false;
    }

    config.sink_index = *sink_index;
    config.sink_name = *sink_name;
    config.buffer_attr.maxlength = sanitize_buffer_value(*maxlength, kDefaultMaxLength);
    config.buffer_attr.tlength = sanitize_buffer_value(*tlength, kDefaultBufferLength);
    config.buffer_attr.prebuf = sanitize_buffer_value(*prebuf, kDefaultPrebuf);
    config.buffer_attr.minreq = sanitize_buffer_value(*minreq, kDefaultMinReq);
    config.sync_id = *sync_id;
    config.volume = *cvolume;

    bool muted = false;
    bool adjust_latency_flag = false;
    bool early_requests_flag = false;
    if (negotiated_version >= 12) {
        auto no_remap = reader.read_bool();
        auto no_remix = reader.read_bool();
        auto fix_format = reader.read_bool();
        auto fix_rate = reader.read_bool();
        auto fix_channels = reader.read_bool();
        auto no_move = reader.read_bool();
        auto variable_rate = reader.read_bool();
        if (!no_remap || !no_remix || !fix_format || !fix_rate || !fix_channels || !no_move || !variable_rate) {
            enqueue_error(tag, PA_ERR_PROTOCOL);
            return false;
        }
    }
    if (negotiated_version >= 13) {
        auto muted_opt = reader.read_bool();
        auto adjust_opt = reader.read_bool();
        auto props = reader.read_proplist();
        if (!muted_opt || !adjust_opt || !props) {
            enqueue_error(tag, PA_ERR_PROTOCOL);
            return false;
        }
        muted = *muted_opt;
        adjust_latency_flag = *adjust_opt;
        config.proplist = std::move(*props);
    }

    if (negotiated_version >= 14) {
        auto volume_set = reader.read_bool();
        auto early_requests = reader.read_bool();
        if (!volume_set || !early_requests) {
            enqueue_error(tag, PA_ERR_PROTOCOL);
            return false;
        }
        early_requests_flag = *early_requests;
    }
    if (negotiated_version >= 15) {
        auto muted_set_flag = reader.read_bool();
        auto dont_inhibit_auto_suspend = reader.read_bool();
        auto fail_on_suspend = reader.read_bool();
        if (!muted_set_flag || !dont_inhibit_auto_suspend || !fail_on_suspend) {
            enqueue_error(tag, PA_ERR_PROTOCOL);
            return false;
        }
    }
    if (negotiated_version >= 17) {
        auto relative_volume = reader.read_bool();
        if (!relative_volume) {
            enqueue_error(tag, PA_ERR_PROTOCOL);
            return false;
        }
    }
    if (negotiated_version >= 18) {
        auto passthrough = reader.read_bool();
        if (!passthrough) {
            enqueue_error(tag, PA_ERR_PROTOCOL);
            return false;
        }
        if (*passthrough) {
            reader.skip_remaining();
            enqueue_error(tag, PA_ERR_NOTSUPPORTED);
            return true;
        }
    }
    if (negotiated_version >= 21) {
        auto formats = reader.read_u8();
        if (!formats) {
            enqueue_error(tag, PA_ERR_PROTOCOL);
            return false;
        }
        if (*formats > 0) {
            owner->log_warning("CreatePlaybackStream format negotiation not supported");
            reader.skip_remaining();
            enqueue_error(tag, PA_ERR_NOTSUPPORTED);
            return true;
        }
    }

    if (!reader.eof()) {
        owner->log_debug("CreatePlaybackStream trailing payload " + std::to_string(reader.bytes_remaining()) + " bytes");
        reader.skip_remaining();
    }

    const bool format_supported = sample_format_supported(config.sample_spec);
    const bool map_matches = format_supported &&
                             (config.channel_map.channels == config.sample_spec.channels);

    if (!format_supported || !map_matches) {
        enqueue_error(tag, PA_ERR_NOTSUPPORTED);
        return true;
    }

    if (config.sink_index != kInvalidIndex && config.sink_index != kVirtualSinkIndex) {
        enqueue_error(tag, PA_ERR_NOENTITY);
        return true;
    }
    if (!config.sink_name.empty() && config.sink_name != kVirtualSinkName) {
        enqueue_error(tag, PA_ERR_NOENTITY);
        return true;
    }

    StreamState stream;
    stream.local_index = next_stream_index++;
    stream.sink_input_index = next_sink_input_index++;
    stream.buffer_attr = config.buffer_attr;
    stream.sample_spec = config.sample_spec;
    stream.channel_map = config.channel_map;
    stream.volume = config.volume;
    stream.corked = muted;
    stream.base_tag = composite_tag_for_stream(config.proplist);
    strip_nuls(stream.base_tag);
    stream.wildcard_tag = make_wildcard_tag(stream.base_tag);
    stream.composite_tag = make_unique_stream_tag(stream.base_tag);
    strip_nuls(stream.composite_tag);
    stream.proplist = config.proplist;
    stream.adjust_latency = adjust_latency_flag;
    stream.early_requests = early_requests_flag;
    stream.started_notified = false;
    stream.playback_started = false;
    stream.playback_start_time = {};
    stream.underrun_usec = 0;

    auto media_name_it = stream.proplist.find("media.name");
    if (media_name_it != stream.proplist.end()) {
        stream.stream_name = media_name_it->second;
    }

    auto layout = guess_channel_layout(stream.channel_map);
    stream.chlayout1 = layout.first;
    stream.chlayout2 = layout.second;

    owner->note_tag_seen(stream.wildcard_tag);
    owner->register_tag_mapping(stream.wildcard_tag, stream.composite_tag);

    LOG_CPP_INFO("Accepted PulseAudio client tag %s", stream.composite_tag.c_str());

    auto [stream_it, inserted] = streams.emplace(stream.local_index, stream);
    stream_it->second.frame_cursor = 0;
    stream_it->second.pending_payload.clear();
    stream_it->second.pending_payload.reserve(CHUNK_SIZE * 2);
    stream_it->second.has_last_delivery = false;
    uint32_t initial_request = effective_request_bytes(stream_it->second);
    stream_it->second.pending_request_bytes = initial_request;
    stream_it->second.next_request_time = std::chrono::steady_clock::now();
    stream_it->second.samples_per_chunk = calculate_samples_per_chunk(stream_it->second);
    stream_it->second.has_rtp_frame = false;
    // Initialize RTP base to a randomized 32-bit value and set the extended
    // timeline start. Using a random offset avoids timestamp collisions and
    // better matches RTP expectations while remaining purely local here.
    {
        std::random_device rd;
        std::mt19937_64 gen(static_cast<uint64_t>(rd()) ^
                            (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this)) << 1));
        std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFFu);
        uint32_t base = dist(gen);
        stream_it->second.rtp_base = static_cast<uint64_t>(base);
        stream_it->second.next_rtp_frame = stream_it->second.rtp_base;
    }
    if (owner->clock_manager && stream_it->second.samples_per_chunk > 0) {
        register_stream_clock(stream_it->second);
    } else if (stream_it->second.samples_per_chunk == 0) {
        owner->log_error("Unsupported PulseAudio format for clock scheduling on stream " + stream_it->second.composite_tag);
    }

    TagWriter writer;
    writer.put_command(Command::Reply, tag);
    writer.put_u32(stream.local_index);
    writer.put_u32(stream.sink_input_index);
    writer.put_u32(initial_request);

    if (negotiated_version >= 9) {
        writer.put_u32(stream.buffer_attr.maxlength);
        writer.put_u32(stream.buffer_attr.tlength);
        writer.put_u32(stream.buffer_attr.prebuf);
        writer.put_u32(stream.buffer_attr.minreq);
    }
    if (negotiated_version >= 12) {
        writer.put_sample_spec(stream.sample_spec);
        writer.put_channel_map(stream.channel_map);
        writer.put_u32(kVirtualSinkIndex);
        writer.put_string(kVirtualSinkName);
        writer.put_bool(false);
    }
    if (negotiated_version >= 13) {
        writer.put_usec(0);
    }

    enqueue_tagstruct(writer);

    return true;
}

bool PulseAudioReceiver::Impl::Connection::handle_delete_stream(uint32_t tag, TagReader& reader) {
    std::lock_guard<std::mutex> stream_lock(stream_mutex);
    auto channel = reader.read_u32();
    if (!channel || !reader.eof()) {
        owner->log_warning("Cork parse failure");
        enqueue_error(tag, PA_ERR_PROTOCOL);
        return false;
    }
    auto it = streams.find(*channel);
    if (it != streams.end()) {
        const std::string stream_tag = it->second.composite_tag;
        unregister_stream_clock(it->second);
        auto removed_stream = std::move(it->second);
        std::string wildcard_tag = removed_stream.wildcard_tag;
        streams.erase(it);
        if (owner->timeshift_manager) {
            owner->timeshift_manager->reset_stream_state(stream_tag);
        }
        owner->unregister_tag_mapping(wildcard_tag, stream_tag);
        owner->note_tag_removed(wildcard_tag);
    }
    enqueue_simple_reply(tag);
    return true;
}

bool PulseAudioReceiver::Impl::Connection::handle_cork_stream(uint32_t tag, TagReader& reader) {
    std::lock_guard<std::mutex> stream_lock(stream_mutex);
    auto channel = reader.read_u32();
    auto cork = reader.read_bool();
    if (!channel || !cork || !reader.eof()) {
        owner->log_warning("Flush parse failure");
        enqueue_error(tag, PA_ERR_PROTOCOL);
        return false;
    }
    auto it = streams.find(*channel);
    if (it == streams.end()) {
        enqueue_error(tag, PA_ERR_NOENTITY);
        return true;
    }
    it->second.corked = *cork;
    if (*cork) {
        it->second.started_notified = false;
        it->second.playback_started = false;
        it->second.has_last_delivery = false;
        it->second.playback_start_time = std::chrono::steady_clock::time_point{};
        it->second.underrun_usec = 0;
    }
    enqueue_simple_reply(tag);
    return true;
}

bool PulseAudioReceiver::Impl::Connection::handle_flush_stream(uint32_t tag, TagReader& reader) {
    std::lock_guard<std::mutex> stream_lock(stream_mutex);
    auto channel = reader.read_u32();
    if (!channel || !reader.eof()) {
        owner->log_warning("Drain parse failure");
        enqueue_error(tag, PA_ERR_PROTOCOL);
        return false;
    }
    auto it = streams.find(*channel);
    if (it == streams.end()) {
        enqueue_error(tag, PA_ERR_NOENTITY);
        return true;
    }
    it->second.started_notified = false;
    enqueue_simple_reply(tag);
    return true;
}

bool PulseAudioReceiver::Impl::Connection::handle_drain_stream(uint32_t tag, TagReader& reader) {
    std::lock_guard<std::mutex> stream_lock(stream_mutex);
    auto channel = reader.read_u32();
    if (!channel || !reader.eof()) {
        owner->log_warning("SetBufferAttr parse failure");
        enqueue_error(tag, PA_ERR_PROTOCOL);
        return false;
    }
    if (streams.find(*channel) == streams.end()) {
        enqueue_error(tag, PA_ERR_NOENTITY);
        return true;
    }
    enqueue_simple_reply(tag);
    return true;
}

bool PulseAudioReceiver::Impl::Connection::handle_set_buffer_attr(uint32_t tag, TagReader& reader) {
    std::lock_guard<std::mutex> stream_lock(stream_mutex);
    auto channel = reader.read_u32();
    auto maxlength = reader.read_u32();
    auto tlength = reader.read_u32();
    auto prebuf = reader.read_u32();
    auto minreq = reader.read_u32();
    bool adjust_latency_flag = false;
    bool early_requests_flag = false;
    if (!channel || !maxlength || !tlength || !prebuf || !minreq) {
        enqueue_error(tag, PA_ERR_PROTOCOL);
        return false;
    }
    if (negotiated_version >= 13) {
        auto adjust_opt = reader.read_bool();
        if (!adjust_opt) {
            enqueue_error(tag, PA_ERR_PROTOCOL);
            return false;
        }
        adjust_latency_flag = *adjust_opt;
    }
    if (negotiated_version >= 14) {
        auto early_opt = reader.read_bool();
        if (!early_opt) {
            enqueue_error(tag, PA_ERR_PROTOCOL);
            return false;
        }
        early_requests_flag = *early_opt;
    }
    if (!reader.eof()) {
        enqueue_error(tag, PA_ERR_PROTOCOL);
        return false;
    }
    auto it = streams.find(*channel);
    if (it == streams.end()) {
        enqueue_error(tag, PA_ERR_NOENTITY);
        return true;
    }
    auto& stream = it->second;
    stream.buffer_attr.maxlength = sanitize_buffer_value(*maxlength, kDefaultMaxLength);
    stream.buffer_attr.tlength = sanitize_buffer_value(*tlength, kDefaultBufferLength);
    stream.buffer_attr.prebuf = sanitize_buffer_value(*prebuf, kDefaultPrebuf);
    stream.buffer_attr.minreq = sanitize_buffer_value(*minreq, kDefaultMinReq);
    stream.adjust_latency = adjust_latency_flag;
    stream.early_requests = early_requests_flag;

    TagWriter writer;
    writer.put_command(Command::Reply, tag);
    writer.put_u32(stream.buffer_attr.maxlength);
    writer.put_u32(stream.buffer_attr.tlength);
    writer.put_u32(stream.buffer_attr.prebuf);
    writer.put_u32(stream.buffer_attr.minreq);
    if (negotiated_version >= 13) {
        uint64_t latency_usec = 0;
        const uint32_t rate = stream.sample_spec.rate;
        const uint32_t bit_depth = sample_format_bit_depth(stream.sample_spec.format);
        const uint32_t bytes_per_frame = stream.sample_spec.channels * std::max<uint32_t>(bit_depth / 8, 1);
        if (rate > 0 && bytes_per_frame > 0) {
            const uint64_t frames = stream.buffer_attr.tlength / bytes_per_frame;
            latency_usec = static_cast<uint64_t>((static_cast<double>(frames) * 1000000.0) / static_cast<double>(rate));
        }
        writer.put_usec(latency_usec);
    }
    enqueue_tagstruct(writer);

    stream.pending_request_bytes = effective_request_bytes(stream);
    stream.next_request_time = std::chrono::steady_clock::now();
    return true;
}

bool PulseAudioReceiver::Impl::Connection::handle_get_playback_latency(uint32_t tag, TagReader& reader) {
    std::lock_guard<std::mutex> stream_lock(stream_mutex);
    if (!ensure_authorized(tag)) {
        return true;
    }
    auto channel = reader.read_u32();
    timeval request_time{};
    if (!channel) {
        enqueue_error(tag, PA_ERR_PROTOCOL);
        return false;
    }
    if (auto tv = reader.read_timeval()) {
        request_time = *tv;
    }
    reader.skip_remaining();
    auto it = streams.find(*channel);
    if (it == streams.end()) {
        enqueue_error(tag, PA_ERR_NOENTITY);
        return true;
    }

    auto& stream = it->second;
    record_latency_query(stream);
    const uint32_t bit_depth = sample_format_bit_depth(stream.sample_spec.format);
    const uint32_t bytes_per_sample = std::max<uint32_t>(bit_depth / 8, 1);
    const uint32_t bytes_per_frame = stream.sample_spec.channels * bytes_per_sample;
    const auto now_steady = std::chrono::steady_clock::now();
    if (stream.has_last_delivery && stream.last_delivery_time < now_steady) {
        auto underrun = std::chrono::duration_cast<std::chrono::microseconds>(now_steady - stream.last_delivery_time);
        if (underrun.count() > 0) {
            stream.underrun_usec += static_cast<uint64_t>(underrun.count());
            stream.last_delivery_time = now_steady;
        }
    }

    uint64_t converted_latency_usec = 0;
    if (stream.has_last_delivery && stream.last_delivery_time > now_steady) {
        converted_latency_usec = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(stream.last_delivery_time - now_steady).count());
    }

    uint64_t pending_frames = 0;
    if (bytes_per_frame > 0) {
        pending_frames = stream.pending_payload.size() / bytes_per_frame;
    }
    uint64_t pending_usec = 0;
    if (stream.sample_spec.rate > 0) {
        pending_usec = (pending_frames * 1'000'000ULL) / stream.sample_spec.rate;
    }
    const uint64_t total_latency_usec = converted_latency_usec + pending_usec;

    uint64_t write_index_bytes = 0;
    uint64_t read_index_bytes = 0;
    if (bytes_per_frame > 0) {
        const uint64_t max_frames_for_index = std::numeric_limits<uint64_t>::max() / bytes_per_frame;
        const uint64_t capped_write_frames = std::min<uint64_t>(stream.frame_cursor, max_frames_for_index);
        write_index_bytes = capped_write_frames * bytes_per_frame;

        uint64_t buffered_frames = 0;
        if (stream.sample_spec.rate > 0) {
            long double buffered = (static_cast<long double>(converted_latency_usec) *
                                    static_cast<long double>(stream.sample_spec.rate)) / 1'000'000.0L;
            if (buffered > static_cast<long double>(stream.frame_cursor)) {
                buffered = static_cast<long double>(stream.frame_cursor);
            }
            if (buffered > 0.0L) {
                buffered_frames = static_cast<uint64_t>(buffered);
            }
        }
        if (buffered_frames > stream.frame_cursor) {
            buffered_frames = stream.frame_cursor;
        }
        const uint64_t readable_frames = stream.frame_cursor >= buffered_frames
            ? stream.frame_cursor - buffered_frames
            : 0;
        const uint64_t capped_read_frames = std::min<uint64_t>(readable_frames, max_frames_for_index);
        read_index_bytes = capped_read_frames * bytes_per_frame;
    }

    const bool running = stream.playback_started && !stream.corked;

    uint64_t playing_for_usec = 0;
    if (stream.playback_started) {
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now_steady - stream.playback_start_time);
        if (elapsed.count() > 0) {
            playing_for_usec = static_cast<uint64_t>(elapsed.count());
        }
        if (playing_for_usec > stream.underrun_usec) {
            playing_for_usec -= stream.underrun_usec;
        } else {
            playing_for_usec = 0;
        }
    }

    timeval now{};
    gettimeofday(&now, nullptr);

    TagWriter writer;
    writer.put_command(Command::Reply, tag);
    writer.put_usec(total_latency_usec);
    writer.put_usec(0);
    writer.put_bool(running);
    writer.put_timeval(request_time);
    writer.put_timeval(now);
    writer.put_s64(static_cast<int64_t>(write_index_bytes));
    writer.put_s64(static_cast<int64_t>(read_index_bytes));
    if (negotiated_version >= 13) {
        writer.put_u64(stream.underrun_usec);
        writer.put_u64(playing_for_usec);
    }
    enqueue_tagstruct(writer);
    maybe_log_stream_profile(*channel, stream, now_steady);
    return true;
}

bool PulseAudioReceiver::Impl::Connection::handle_set_sink_input_volume(uint32_t tag, TagReader& reader) {
    std::lock_guard<std::mutex> stream_lock(stream_mutex);
    if (!ensure_authorized(tag)) {
        return true;
    }
    auto channel = reader.read_u32();
    auto volume = reader.read_cvolume();
    if (!channel || !volume || !reader.eof()) {
        enqueue_error(tag, PA_ERR_PROTOCOL);
        return false;
    }

    enqueue_simple_reply(tag);
    return true;
}

bool PulseAudioReceiver::Impl::Connection::handle_set_stream_name(uint32_t tag, TagReader& reader) {
    std::lock_guard<std::mutex> stream_lock(stream_mutex);
    if (!ensure_authorized(tag)) {
        return true;
    }
    auto channel = reader.read_u32();
    auto name = reader.read_string();
    if (!channel || !name || !reader.eof()) {
        enqueue_error(tag, PA_ERR_PROTOCOL);
        return false;
    }
    auto it = streams.find(*channel);
    if (it == streams.end()) {
        enqueue_error(tag, PA_ERR_NOENTITY);
        return true;
    }
    it->second.stream_name = *name;
    it->second.proplist["media.name"] = *name;
    enqueue_simple_reply(tag);
    return true;
}

bool PulseAudioReceiver::Impl::Connection::handle_update_playback_stream_proplist(uint32_t tag, TagReader& reader) {
    std::lock_guard<std::mutex> stream_lock(stream_mutex);
    if (!ensure_authorized(tag)) {
        return true;
    }

    auto channel = reader.read_u32();
    auto mode = reader.read_u32();
    auto properties = reader.read_proplist();

    if (!channel || !mode || !properties || !reader.eof()) {
        enqueue_error(tag, PA_ERR_PROTOCOL);
        return false;
    }
    if (*mode != kUpdateSet && *mode != kUpdateMerge && *mode != kUpdateReplace) {
        enqueue_error(tag, PA_ERR_INVALID);
        return false;
    }

    auto it = streams.find(*channel);
    if (it == streams.end()) {
        enqueue_error(tag, PA_ERR_NOENTITY);
        return true;
    }

    apply_proplist_update(it->second.proplist, *properties, *mode);
    if (!properties->empty()) {
        auto name_it = it->second.proplist.find("media.name");
        if (name_it != it->second.proplist.end()) {
            it->second.stream_name = name_it->second;
        }
    }

    enqueue_simple_reply(tag);
    return true;
}

bool PulseAudioReceiver::Impl::Connection::handle_update_client_proplist(uint32_t tag, TagReader& reader) {
    if (!ensure_authorized(tag)) {
        return true;
    }

    auto mode = reader.read_u32();
    auto properties = reader.read_proplist();

    if (!mode || !properties || !reader.eof()) {
        enqueue_error(tag, PA_ERR_PROTOCOL);
        return false;
    }
    if (*mode != kUpdateSet && *mode != kUpdateMerge && *mode != kUpdateReplace) {
        enqueue_error(tag, PA_ERR_INVALID);
        return false;
    }

    apply_proplist_update(client_props, *properties, *mode);

    auto name_it = client_props.find("application.name");
    client_app_name = name_it != client_props.end() ? name_it->second : std::string();

    auto binary_it = client_props.find("application.process.binary");
    client_process_binary = binary_it != client_props.end() ? binary_it->second : std::string();

    enqueue_simple_reply(tag);
    return true;
}

bool PulseAudioReceiver::Impl::Connection::handle_register_memfd(uint32_t tag, TagReader& reader, std::vector<int>& fds) {
    (void)tag;
    if (!use_memfd) {
        owner->log_warning("Received REGISTER_MEMFD_SHMID but memfd is disabled for this connection");
        return true;
    }

    auto shm_id_opt = reader.read_u32();
    if (!shm_id_opt || !reader.eof()) {
        owner->log_warning("REGISTER_MEMFD_SHMID parse failure");
        enqueue_error(tag, PA_ERR_PROTOCOL);
        return false;
    }

    if (fds.size() != 1 || fds[0] < 0) {
        owner->log_warning("REGISTER_MEMFD_SHMID missing file descriptor");
        enqueue_error(tag, PA_ERR_PROTOCOL);
        return false;
    }

    int fd = fds[0];
    struct stat st {};
    if (::fstat(fd, &st) < 0) {
        owner->log_warning("REGISTER_MEMFD_SHMID fstat failed: " + errno_string(errno));
        return false;
    }

    if (st.st_size <= 0) {
        owner->log_warning("REGISTER_MEMFD_SHMID invalid memfd size");
        return false;
    }

    auto it = memfd_pools.find(*shm_id_opt);
    if (it != memfd_pools.end()) {
        if (it->second.fd >= 0) {
            ::close(it->second.fd);
        }
        memfd_pools.erase(it);
    }

    MemfdPool pool;
    pool.fd = fd;
    pool.size = st.st_size;
    memfd_pools.emplace(*shm_id_opt, pool);

    owner->log("Registered memfd pool id=" + std::to_string(*shm_id_opt) +
               " size=" + std::to_string(static_cast<long long>(st.st_size)));

    fds.clear();
    return true;
}

bool PulseAudioReceiver::Impl::Connection::handle_playback_data(const Message& message) {
    std::lock_guard<std::mutex> stream_lock(stream_mutex);
    const uint32_t flags = message.descriptor.flags;

    if ((flags & kDescriptorFlagShmMask) == kDescriptorFlagShmRelease ||
        (flags & kDescriptorFlagShmMask) == kDescriptorFlagShmRevoke) {
        // Ignore release/revoke notifications from the client; nothing to do.
        return true;
    }

    uint32_t stream_index = message.descriptor.channel;
    auto it = streams.find(stream_index);
    if (it == streams.end()) {
        owner->log_warning("Audio data for unknown stream");
        return false;
    }
    auto& stream = it->second;

    std::vector<uint8_t> memfd_payload;
    const std::vector<uint8_t>* active_payload = &message.payload;
    bool should_release_block = false;
    uint32_t release_block_id = 0;

    if ((flags & kDescriptorFlagShmData) != 0) {
        if (!use_memfd || (flags & kDescriptorFlagMemfdBlock) == 0) {
            owner->log_warning("Received SHM block without memfd support");
            return false;
        }
        if (message.payload.size() < sizeof(uint32_t) * 4) {
            owner->log_warning("Malformed SHM block payload");
            return false;
        }

        uint32_t info[4];
        std::memcpy(info, message.payload.data(), sizeof(info));
        uint32_t block_id = ntohl(info[kShmInfoBlockIdIndex]);
        uint32_t shm_id = ntohl(info[kShmInfoShmIdIndex]);
        uint32_t offset = ntohl(info[kShmInfoOffsetIndex]);
        uint32_t length = ntohl(info[kShmInfoLengthIndex]);

        if (length == 0) {
            owner->log_warning("Received zero-length memfd block");
            return false;
        }

        if (owner->debug_packets) {
            owner->log_debug("RECV memfd block stream=" + std::to_string(stream_index) +
                             " shm=" + std::to_string(shm_id) +
                             " block=" + std::to_string(block_id) +
                             " len=" + std::to_string(length) +
                             " off=" + std::to_string(offset));
        }

        auto pool_it = memfd_pools.find(shm_id);
        if (pool_it == memfd_pools.end()) {
            if (!non_registered_memfd_error_logged) {
                owner->log_warning("Memfd pool " + std::to_string(shm_id) + " not registered");
                non_registered_memfd_error_logged = true;
            }
            return false;
        }

        const MemfdPool& pool = pool_it->second;
        const off_t end_offset = static_cast<off_t>(offset) + static_cast<off_t>(length);
        if (end_offset < 0 || end_offset > pool.size) {
            owner->log_warning("Memfd block range invalid for pool");
            return false;
        }

        memfd_payload.resize(length);
        ssize_t read_bytes = ::pread(pool.fd, memfd_payload.data(), length, static_cast<off_t>(offset));
        if (read_bytes < 0) {
            owner->log_warning("Failed reading memfd block: " + errno_string(errno));
            return false;
        }
        if (read_bytes != static_cast<ssize_t>(length)) {
            owner->log_warning("Failed reading memfd block: short read");
            return false;
        }

        active_payload = &memfd_payload;
        should_release_block = true;
        release_block_id = block_id;
    }

    auto now = std::chrono::steady_clock::now();
    const size_t channels = stream.sample_spec.channels;
    const uint32_t bit_depth = sample_format_bit_depth(stream.sample_spec.format);
    const size_t bytes_per_sample = std::max<uint32_t>(bit_depth / 8, 1);
    const size_t frame_bytes = channels * bytes_per_sample;
    size_t processed_frames = 0;
    uint64_t frames_produced = 0;
    const bool from_memfd = (flags & kDescriptorFlagShmData) != 0;
    const bool converted_format = (stream.sample_spec.format == kSampleFormatFloat32LE);

    if (!active_payload->empty()) {
        stream.pending_payload.write(active_payload->data(), active_payload->size());
    }

    if (should_release_block) {
        enqueue_shm_release(release_block_id);
    }

    while (stream.pending_payload.size() >= CHUNK_SIZE) {
        std::vector<uint8_t> chunk(CHUNK_SIZE);
        const std::size_t popped = stream.pending_payload.pop(chunk.data(), CHUNK_SIZE);
        if (popped == 0) {
            break;
        }
        chunk.resize(popped);

        if (stream.sample_spec.format == kSampleFormatFloat32LE) {
            chunk = convert_float_chunk_to_s32(chunk);
        }

        const size_t chunk_bytes = chunk.size();
        const uint64_t chunk_frames = frame_bytes > 0 ? static_cast<uint64_t>(chunk_bytes / frame_bytes) : 0;
        if (chunk_frames > 0) {
            const uint64_t chunk_start_frame = stream.frame_cursor;
            stream.frame_cursor += chunk_frames;
            processed_frames += static_cast<size_t>(chunk_frames);
            frames_produced += chunk_frames;

            std::chrono::steady_clock::time_point chunk_start_time = stream.last_delivery_time;
            uint64_t catchup_for_chunk = 0;
            if (stream.sample_spec.rate > 0) {
                if (!stream.has_last_delivery) {
                    stream.last_delivery_time = now;
                    stream.has_last_delivery = true;
                }
                if (stream.last_delivery_time < now) {
                    auto underrun = std::chrono::duration_cast<std::chrono::microseconds>(now - stream.last_delivery_time);
                    if (underrun.count() > 0) {
                        stream.underrun_usec += static_cast<uint64_t>(underrun.count());

                        if (underrun.count() > kMaxUnderrunResetUsec) {
                            // For large gaps, snap to realtime so new streams don't start seconds behind.
                            stream.last_delivery_time = now;
                        } else {
                            const int64_t catch_up = std::min<int64_t>(underrun.count(), kMaxCatchupUsecPerChunk);
                            stream.last_delivery_time += std::chrono::microseconds(catch_up);
                            if (catch_up > 0) {
                                catchup_for_chunk += static_cast<uint64_t>(catch_up);
                            }
                            if (stream.last_delivery_time > now) {
                                stream.last_delivery_time = now;
                            }
                        }
                    }
                }

                const uint64_t chunk_usec = (chunk_frames * 1'000'000ULL) / stream.sample_spec.rate;
                stream.last_delivery_time += std::chrono::microseconds(chunk_usec);
                chunk_start_time = stream.last_delivery_time - std::chrono::microseconds(chunk_usec);

                if (!stream.playback_started) {
                    stream.playback_started = true;
                    stream.playback_start_time = now;
                }
            }

            Connection::PendingChunk pending_chunk;
            pending_chunk.audio_data = std::move(chunk);
            pending_chunk.start_frame = chunk_start_frame;
            pending_chunk.chunk_bytes = chunk_bytes;
            pending_chunk.chunk_frames = chunk_frames;
            pending_chunk.from_memfd = from_memfd;
            pending_chunk.converted = converted_format;
            pending_chunk.catchup_usec = catchup_for_chunk;
            pending_chunk.play_time = chunk_start_time;

            stream.pending_chunks.push_back(std::move(pending_chunk));
        }
    }

    if (processed_frames > 0 && !stream.started_notified) {
        enqueue_started(stream.local_index);
        stream.started_notified = true;
    }

    if (frames_produced > 0 && stream.sample_spec.rate > 0) {
        const double seconds = static_cast<double>(frames_produced) / static_cast<double>(stream.sample_spec.rate);
        stream.next_request_time = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(seconds));
    } else {
        stream.next_request_time = now;
    }
    const uint32_t request_bytes = effective_request_bytes(stream);
    stream.pending_request_bytes = std::max(stream.pending_request_bytes, request_bytes);

    maybe_log_stream_profile(stream_index, stream, std::chrono::steady_clock::now());

    return true;
}

std::string PulseAudioReceiver::Impl::Connection::composite_tag_for_stream(const std::unordered_map<std::string, std::string>& proplist) const {
    std::string program = client_process_binary;
    if (program.empty()) {
        auto it = proplist.find("application.process.binary");
        if (it != proplist.end()) program = it->second;
    }
    if (program.empty()) {
        program = client_app_name.empty() ? "PulseClient" : client_app_name;
    }
    strip_nuls(program);
    program = trim_string(program);
    
    std::string base = base_identity.empty() ? peer_identity : base_identity;
    strip_nuls(base);
    base = trim_string(base);

    std::string composite = base;
    if (!program.empty()) {
        if (!composite.empty()) {
            composite += ' ';
        }
        composite += program;
    }
    return composite;
}

void PulseAudioReceiver::Impl::Connection::process_due_requests() {
    std::lock_guard<std::mutex> stream_lock(stream_mutex);
    if (streams.empty()) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    for (auto& [stream_index, stream] : streams) {
        if (stream.pending_request_bytes == 0 || stream.next_request_time > now) {
            maybe_log_stream_profile(stream_index, stream, now);
            continue;
        }
        enqueue_request(stream_index, stream.pending_request_bytes);
        stream.pending_request_bytes = 0;
        maybe_log_stream_profile(stream_index, stream, now);
    }
}

std::optional<std::chrono::steady_clock::time_point>
PulseAudioReceiver::Impl::Connection::next_due_request() const {
    std::lock_guard<std::mutex> stream_lock(stream_mutex);
    std::optional<std::chrono::steady_clock::time_point> earliest;
    for (const auto& [_, stream] : streams) {
        if (stream.pending_request_bytes == 0) {
            continue;
        }
        if (!earliest || stream.next_request_time < *earliest) {
            earliest = stream.next_request_time;
        }
    }
    return earliest;
}

void PulseAudioReceiver::Impl::event_loop(std::atomic<bool>& stop_flag) {
    while (!stop_flag.load()) {
        for (auto& connection : connections) {
            if (connection) {
                connection->dispatch_clock_ticks();
            }
        }

        std::vector<pollfd> pollfds;
        if (tcp_listen_fd >= 0) {
            pollfds.push_back(pollfd{tcp_listen_fd, POLLIN, 0});
        }
        if (unix_listen_fd >= 0) {
            pollfds.push_back(pollfd{unix_listen_fd, POLLIN, 0});
        }
        for (auto& connection : connections) {
            pollfd pfd{};
            pfd.fd = connection->fd;
            pfd.events = connection->desired_poll_events();
            pollfds.push_back(pfd);
        }

        auto now = std::chrono::steady_clock::now();
        int timeout_ms = 5;
        for (const auto& connection : connections) {
            if (auto due = connection->next_due_request()) {
                if (*due <= now) {
                    timeout_ms = 0;
                    break;
                }
                auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(*due - now);
                if (diff.count() < timeout_ms) {
                    timeout_ms = static_cast<int>(std::max<long long>(diff.count(), 0));
                }
            }
            if (timeout_ms == 0) {
                continue;
            }
        }

        int rc = ::poll(pollfds.data(), pollfds.size(), timeout_ms);
        if (rc < 0) {
            if (errno == EINTR) continue;
            log_warning("poll failed: " + errno_string(errno));
            continue;
        }

        std::size_t index = 0;
        if (tcp_listen_fd >= 0) {
            if (pollfds[index].revents & POLLIN) {
                accept_connections(tcp_listen_fd, false);
            }
            ++index;
        }
        if (unix_listen_fd >= 0) {
            if (pollfds[index].revents & POLLIN) {
                accept_connections(unix_listen_fd, true);
            }
            ++index;
        }

        for (std::size_t i = 0; i < connections.size(); ) {
            auto& conn = connections[i];
            short revents = pollfds[index + i].revents;
            if (revents != 0) {
                if (!conn->handle_io(revents)) {
                    remove_connection(i);
                    continue;
                }
            }
            conn->process_due_requests();
            conn->dispatch_clock_ticks();
            ++i;
        }
    }
}

PulseAudioReceiver::PulseAudioReceiver(PulseReceiverConfig config,
                                       std::shared_ptr<NotificationQueue> notification_queue,
                                       TimeshiftManager* timeshift_manager,
                                       ClockManager* clock_manager,
                                       std::string logger_prefix)
    : impl_(std::make_unique<Impl>()),
      config_(config) {
    impl_->config = std::move(config);
    impl_->notification_queue = std::move(notification_queue);
    impl_->timeshift_manager = timeshift_manager;
    impl_->clock_manager = clock_manager;
    impl_->logger_prefix = std::move(logger_prefix);
}

PulseAudioReceiver::~PulseAudioReceiver() {
    stop();
}

void PulseAudioReceiver::start() {
    if (is_running()) {
        return;
    }
    if (!impl_->initialize()) {
        impl_->shutdown_all();
        return;
    }
    stop_flag_ = false;
    component_thread_ = std::thread([this]() { run(); });
}

void PulseAudioReceiver::stop() {
    if (stop_flag_) {
        return;
    }
    stop_flag_ = true;
    if (component_thread_.joinable()) {
        component_thread_.join();
    }
    impl_->shutdown_all();
}

std::vector<std::string> PulseAudioReceiver::get_seen_tags() {
    std::vector<std::string> tags;
    tags.swap(impl_->seen_tags);
    return tags;
}

std::optional<std::string> PulseAudioReceiver::resolve_stream_tag(const std::string& tag) const {
    impl_->log_debug("resolve_stream_tag called for '" + tag + "'");
    return impl_->resolve_stream_tag_internal(tag);
}

std::vector<std::string> PulseAudioReceiver::list_stream_tags_for_wildcard(const std::string& wildcard) const {
    return impl_->list_streams_for_wildcard(wildcard);
}

void PulseAudioReceiver::set_stream_tag_callbacks(StreamTagResolvedCallback on_resolved,
                                                  StreamTagRemovedCallback on_removed) {
    impl_->stream_tag_resolved_cb = std::move(on_resolved);
    impl_->stream_tag_removed_cb = std::move(on_removed);
}

void PulseAudioReceiver::run() {
    impl_->event_loop(stop_flag_);
}

} // namespace pulse
} // namespace audio
} // namespace screamrouter

#endif // POSIX build
