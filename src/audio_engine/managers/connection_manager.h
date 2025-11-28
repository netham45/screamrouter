/**
 * @file connection_manager.h
 * @brief Defines the ConnectionManager class for handling source-to-sink connections.
 * @details This class encapsulates the logic for connecting and disconnecting
 *          source processors to sink mixers, managing the underlying queue subscriptions.
 */
#ifndef CONNECTION_MANAGER_H
#define CONNECTION_MANAGER_H

#include "source_manager.h"
#include "sink_manager.h"
#include "../audio_types.h"
#include "../input_processor/source_input_processor.h"
#include "../utils/packet_ring.h"
#include <string>
#include <mutex>

namespace screamrouter {
namespace audio {
/**
 * @class ConnectionManager
 * @brief Manages the connections between audio sources and sinks.
 * @details This class provides an abstraction for the complex process of linking a
 *          SourceInputProcessor's output queue to a SinkAudioMixer's input. It works
 *          in conjunction with the SourceManager and SinkManager to ensure that
 *          connections are valid and thread-safe.
 */
class ConnectionManager {
public:
    /**
     * @brief Constructs a ConnectionManager.
     * @param manager_mutex A reference to the main AudioManager mutex for thread safety.
     * @param source_manager A pointer to the SourceManager instance.
     * @param sink_manager A pointer to the SinkManager instance.
     * @param source_to_sink_queues A reference to the map of source-to-sink queues.
     * @param sources A reference to the map of active source processors.
     */
    ConnectionManager(
        std::recursive_mutex& manager_mutex,
        SourceManager* source_manager,
        SinkManager* sink_manager,
        std::map<std::string, std::unique_ptr<SourceInputProcessor>>& sources
    );
    /**
     * @brief Destructor.
     */
    ~ConnectionManager();

    /**
     * @brief Connects a source processor to a sink.
     * @param source_instance_id The unique ID of the source processor instance.
     * @param sink_id The unique ID of the sink.
     * @param running A flag indicating if the audio engine is currently running.
     * @return true if the connection was successful, false otherwise.
     */
    bool connect_source_sink(const std::string& source_instance_id, const std::string& sink_id, bool running);
    /**
     * @brief Disconnects a source processor from a sink.
     * @param source_instance_id The unique ID of the source processor instance.
     * @param sink_id The unique ID of the sink.
     * @param running A flag indicating if the audio engine is currently running.
     * @return true if the disconnection was successful, false otherwise.
     */
    bool disconnect_source_sink(const std::string& source_instance_id, const std::string& sink_id, bool running);

private:
    std::recursive_mutex& m_manager_mutex;
    SourceManager* m_source_manager;
    SinkManager* m_sink_manager;
    std::map<std::string, std::unique_ptr<SourceInputProcessor>>& m_sources;
    std::map<std::string, std::shared_ptr<utils::PacketRing<TaggedAudioPacket>>> m_ready_rings;
};

} // namespace audio
} // namespace screamrouter

#endif // CONNECTION_MANAGER_H
