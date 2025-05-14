// src/configuration/audio_engine_config_applier.h
#ifndef AUDIO_ENGINE_CONFIG_APPLIER_H
#define AUDIO_ENGINE_CONFIG_APPLIER_H

#include <string>
#include <vector>
#include <map>
#include <set> // Included as per spec, though not directly used by example structs

// Include necessary headers first
#include "audio_engine_config_types.h" 
// audio_types.h is included via audio_engine_config_types.h

// Forward declare AudioManager instead of including the full header
// This reduces compile-time dependencies if only a reference/pointer is needed.
namespace screamrouter { namespace audio { class AudioManager; } }

namespace screamrouter {
namespace config {

class AudioEngineConfigApplier {
public:
    // Constructor takes a reference to AudioManager
    explicit AudioEngineConfigApplier(audio::AudioManager& audio_manager);
    
    // Destructor declaration
    ~AudioEngineConfigApplier(); 

    // Delete copy and move operations to prevent accidental copying/moving
    AudioEngineConfigApplier(const AudioEngineConfigApplier&) = delete;
    AudioEngineConfigApplier& operator=(const AudioEngineConfigApplier&) = delete;
    AudioEngineConfigApplier(AudioEngineConfigApplier&&) = delete;
    AudioEngineConfigApplier& operator=(AudioEngineConfigApplier&&) = delete;

    /**
     * @brief Applies the desired configuration state to the AudioManager.
     * @param desired_state The target state including sinks, source paths, and connections.
     * @return true if the state was applied successfully (basic check), false otherwise.
     */
    bool apply_state(const DesiredEngineState& desired_state);

private:
    // Reference to the AudioManager instance being controlled
    audio::AudioManager& audio_manager_;

    // --- Internal State Representation (Shadow State) ---
    
    // Struct to hold the internal state of an active source path
    struct InternalSourcePathState {
        AppliedSourcePathParams params; 
        // generated_instance_id is stored within params.generated_instance_id
    };
    // Map storing active source paths, keyed by path_id
    std::map<std::string, InternalSourcePathState> active_source_paths_; 

    // Struct to hold the internal state of an active sink
    struct InternalSinkState {
        AppliedSinkParams params; 
        // connected_source_path_ids is stored within params.connected_source_path_ids
    };
    // Map storing active sinks, keyed by sink_id
    std::map<std::string, InternalSinkState> active_sinks_;     

    // --- Private Helper Methods ---

    // Reconciliation helpers (compare desired state with active state)
    void reconcile_sinks(
        const std::vector<AppliedSinkParams>& desired_sinks,
        std::vector<std::string>& sink_ids_to_remove,
        std::vector<AppliedSinkParams>& sinks_to_add,
        std::vector<AppliedSinkParams>& sinks_to_update);

    void reconcile_source_paths(
        const std::vector<AppliedSourcePathParams>& desired_source_paths,
        std::vector<std::string>& path_ids_to_remove,
        std::vector<AppliedSourcePathParams>& paths_to_add,
        std::vector<AppliedSourcePathParams>& paths_to_update);

    // Processing helpers (apply changes to AudioManager and update internal state)
    void process_sink_removals(const std::vector<std::string>& sink_ids_to_remove);
    void process_sink_additions(const std::vector<AppliedSinkParams>& sinks_to_add);
    void process_sink_updates(const std::vector<AppliedSinkParams>& sinks_to_update);
    
    void process_source_path_removals(const std::vector<std::string>& path_ids_to_remove);
    // Takes non-const ref to update generated_instance_id, returns true on success
    bool process_source_path_addition(AppliedSourcePathParams& path_to_add); 
    void process_source_path_updates(const std::vector<AppliedSourcePathParams>& paths_to_update);

    // Connection reconciliation helper (called by sink add/update)
    // Takes the desired sink state to determine target connections
    void reconcile_connections_for_sink(const AppliedSinkParams& desired_sink_params);
};

} // namespace config
} // namespace screamrouter

#endif // AUDIO_ENGINE_CONFIG_APPLIER_H
