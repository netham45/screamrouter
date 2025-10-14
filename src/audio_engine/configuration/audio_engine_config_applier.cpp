/**
 * @file audio_engine_config_applier.cpp
 * @brief Implements the AudioEngineConfigApplier class for applying audio configuration.
 *
 * This file contains the implementation of the AudioEngineConfigApplier class, which is
 * responsible for reconciling a desired audio engine state with the current state and
 * applying the necessary changes to the AudioManager.
 */
#include "audio_engine_config_applier.h"
#include "../managers/audio_manager.h" // Include full header for AudioManager methods
#include "../utils/cpp_logger.h" // For LOG_CPP_DEBUG, etc.
#include <algorithm> // For std::find_if, std::find
#include <vector>
#include <string>
#include <map>
#include <set>
#include <cmath> // For std::abs
#include <limits> // For std::numeric_limits
#include <pybind11/pybind11.h>

using namespace screamrouter::audio;

namespace screamrouter {
namespace config {

// --- Constructor & Destructor ---

/**
 * @brief Constructs an AudioEngineConfigApplier.
 * @param audio_manager A reference to the AudioManager instance to be controlled.
 */
AudioEngineConfigApplier::AudioEngineConfigApplier(audio::AudioManager& audio_manager)
    : audio_manager_(audio_manager) {
    LOG_CPP_DEBUG("AudioEngineConfigApplier created.");
    // The active_source_paths_ and active_sinks_ maps are default-initialized to be empty.
}

/**
 * @brief Destroys the AudioEngineConfigApplier.
 */
AudioEngineConfigApplier::~AudioEngineConfigApplier() {
    LOG_CPP_DEBUG("AudioEngineConfigApplier destroyed.");
    // This class does not own the audio_manager_, so no deletion is necessary.
}

// --- Public Methods ---

/**
 * @brief Applies a desired state to the audio engine.
 *
 * This is the main entry point for changing the audio engine's configuration. It performs
 * a full reconciliation of sinks and source paths, applying removals, additions, and updates
 * in a safe order to the AudioManager.
 *
 * @param desired_state The complete desired configuration for the audio engine.
 * @return True if the state application process completes.
 * @note Success/failure tracking is not yet fully implemented.
 */
bool AudioEngineConfigApplier::apply_state(DesiredEngineState desired_state) {
    LOG_CPP_DEBUG("Applying new engine state...");

    std::vector<std::string> sink_ids_to_remove;
    std::vector<AppliedSinkParams> sinks_to_add;
    std::vector<AppliedSinkParams> sinks_to_update;

    std::vector<std::string> path_ids_to_remove;
    std::vector<AppliedSourcePathParams> paths_to_add;
    std::vector<AppliedSourcePathParams> paths_to_update;

    // 1. Reconcile to find differences between the desired state and the current active state.
    LOG_CPP_DEBUG("Reconciling sinks...");
    reconcile_sinks(desired_state.sinks, sink_ids_to_remove, sinks_to_add, sinks_to_update);
    LOG_CPP_DEBUG("Reconciling source paths...");
    reconcile_source_paths(desired_state.source_paths, path_ids_to_remove, paths_to_add, paths_to_update);

    // 2. Process removals first to free up resources and avoid dependency issues.
    // Source paths are removed before sinks as a general practice.
    LOG_CPP_DEBUG("Processing source path removals (%zu)...", path_ids_to_remove.size());
    process_source_path_removals(path_ids_to_remove);

    LOG_CPP_DEBUG("Processing sink removals (%zu)...", sink_ids_to_remove.size());
    process_sink_removals(sink_ids_to_remove);

    // 3. Process additions.
    LOG_CPP_DEBUG("Processing source path additions (%zu)...", paths_to_add.size());
    // A temporary map is not strictly needed here as connections are handled during sink processing.
    std::map<std::string, std::string> added_path_id_to_instance_id;
    for (auto& path_param : paths_to_add) { // Pass by reference to update generated_instance_id.
        if (process_source_path_addition(path_param)) {
            // Successfully added, update our active state map immediately.
            active_source_paths_[path_param.path_id] = {path_param};
            // Store the generated ID for the connection phase.
            added_path_id_to_instance_id[path_param.path_id] = path_param.generated_instance_id;
        } else {
            LOG_CPP_ERROR("Failed to add source path: %s. Skipping associated connections.", path_param.path_id.c_str());
            // Error is logged; processing continues with the next item.
        }
    }

    LOG_CPP_DEBUG("Processing sink additions (%zu)...", sinks_to_add.size());
    // Sink additions will also handle initial connections via reconcile_connections_for_sink.
    process_sink_additions(sinks_to_add);

    // 4. Process updates for existing items (parameter changes, connection changes).
    LOG_CPP_DEBUG("Processing source path updates (%zu)...", paths_to_update.size());
    process_source_path_updates(paths_to_update);
    
    LOG_CPP_DEBUG("Processing sink updates (connections) (%zu)...", sinks_to_update.size());
    // Sink updates will primarily handle connection changes via reconcile_connections_for_sink.
    process_sink_updates(sinks_to_update);

    LOG_CPP_DEBUG("Engine state application finished.");
    // Future work: Implement proper success/failure tracking based on helper method results.
    return true; // Placeholder, assumes success.
}

// --- Comparison Helper Functions ---

/**
 * @brief Compares two SinkConfig objects for equality.
 *
 * This function checks if two sink configurations are functionally identical. It is used
 * during reconciliation to determine if a sink needs to be re-created because its
 * fundamental properties have changed.
 *
 * @param a The first SinkConfig.
 * @param b The second SinkConfig.
 * @return True if the configurations are considered equal, false otherwise.
 */
bool compare_sink_configs(const audio::SinkConfig& a, const audio::SinkConfig& b) {
    // Compare all relevant fields that would require a sink re-creation if changed.
    return a.id == b.id && // The ID must match if comparing the same conceptual sink.
           a.output_ip == b.output_ip &&
           a.output_port == b.output_port &&
           a.bitdepth == b.bitdepth &&
           a.samplerate == b.samplerate &&
           a.channels == b.channels &&
           a.chlayout1 == b.chlayout1 &&
           a.chlayout2 == b.chlayout2 &&
           a.enable_mp3 == b.enable_mp3 &&
           a.protocol == b.protocol;
}

/**
 * @brief Compares two lists of connection IDs for equality, ignoring order.
 *
 * @param a The first vector of connection IDs.
 * @param b The second vector of connection IDs.
 * @return True if the connection lists contain the same elements, false otherwise.
 */
bool compare_connections(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    // Convert to sets for efficient, order-independent comparison.
    std::set<std::string> set_a(a.begin(), a.end());
    std::set<std::string> set_b(b.begin(), b.end());
    return set_a == set_b;
}


// --- Method Implementations ---

// --- Sink Management ---

/**
 * @brief Reconciles the desired sink state with the active state.
 *
 * Compares the list of desired sinks against the currently active sinks to determine
 * which sinks need to be added, removed, or updated.
 *
 * @param desired_sinks The vector of sinks in the desired configuration.
 * @param[out] sink_ids_to_remove Output vector for IDs of sinks to be removed.
 * @param[out] sinks_to_add Output vector for parameters of sinks to be added.
 * @param[out] sinks_to_update Output vector for parameters of sinks to be updated.
 */
void AudioEngineConfigApplier::reconcile_sinks(
    const std::vector<AppliedSinkParams>& desired_sinks,
    std::vector<std::string>& sink_ids_to_remove,
    std::vector<AppliedSinkParams>& sinks_to_add,
    std::vector<AppliedSinkParams>& sinks_to_update)
{
    LOG_CPP_DEBUG("Reconciling sinks...");
    sink_ids_to_remove.clear();
    sinks_to_add.clear();
    sinks_to_update.clear();

    // Create a set of desired sink IDs for quick lookup.
    std::set<std::string> desired_sink_ids;
    for (const auto& desired_sink : desired_sinks) {
        desired_sink_ids.insert(desired_sink.sink_id);
    }

    // 1. Identify sinks to remove (present in active state but not in desired state).
    for (const auto& pair : active_sinks_) {
        const std::string& active_sink_id = pair.first;
        if (desired_sink_ids.find(active_sink_id) == desired_sink_ids.end()) {
            // Active sink not found in desired state, so mark it for removal.
            sink_ids_to_remove.push_back(active_sink_id);
        }
    }

    // 2. Identify sinks to add or update.
    for (const auto& desired_sink : desired_sinks) {
        auto active_it = active_sinks_.find(desired_sink.sink_id);
        if (active_it == active_sinks_.end()) {
            // Desired sink not found in active state, so mark it for addition.
            sinks_to_add.push_back(desired_sink);
        } else {
            // Sink exists, check if an update is needed.
            const InternalSinkState& current_state = active_it->second;
            bool config_changed = !compare_sink_configs(current_state.params.sink_engine_config, desired_sink.sink_engine_config);
            bool connections_changed = !compare_connections(current_state.params.connected_source_path_ids, desired_sink.connected_source_path_ids);

            if (config_changed || connections_changed) {
                sinks_to_update.push_back(desired_sink);
            }
        }
    }
    LOG_CPP_DEBUG("Sink reconciliation complete. To remove: %zu, To add: %zu, To update: %zu",
                 sink_ids_to_remove.size(), sinks_to_add.size(), sinks_to_update.size());
}

/**
 * @brief Processes the removal of sinks from the audio engine.
 * @param sink_ids_to_remove A vector of sink IDs to be removed.
 */
void AudioEngineConfigApplier::process_sink_removals(const std::vector<std::string>& sink_ids_to_remove) {
    LOG_CPP_DEBUG("Processing %zu sink removals...", sink_ids_to_remove.size());
    for (const auto& sink_id : sink_ids_to_remove) {
        LOG_CPP_DEBUG("  - Removing sink: %s", sink_id.c_str());
        if (audio_manager_.remove_sink(sink_id)) {
            active_sinks_.erase(sink_id);
            LOG_CPP_DEBUG("    Sink %s removed successfully from AudioManager and internal state.", sink_id.c_str());
        } else {
            LOG_CPP_ERROR("    AudioManager failed to remove sink: %s. Internal state may be inconsistent.", sink_id.c_str());
            // Attempt to remove from internal state anyway to avoid repeated failed attempts.
            active_sinks_.erase(sink_id);
        }
    }
}

/**
 * @brief Processes the addition of new sinks to the audio engine.
 *
 * For each new sink, it adds it to the AudioManager and then reconciles its initial connections.
 * @param sinks_to_add A vector of parameters for the new sinks to be added.
 */
void AudioEngineConfigApplier::process_sink_additions(const std::vector<AppliedSinkParams>& sinks_to_add) {
    LOG_CPP_DEBUG("Processing %zu sink additions...", sinks_to_add.size());
    for (const auto& sink_param : sinks_to_add) {
        LOG_CPP_DEBUG("  - Adding sink: %s", sink_param.sink_id.c_str());
        if (audio_manager_.add_sink(sink_param.sink_engine_config)) {
            // Add to internal state.
            InternalSinkState new_internal_state;
            new_internal_state.params = sink_param; // Store desired params.
            // Crucially, clear connections initially; reconcile_connections will set them.
            new_internal_state.params.connected_source_path_ids.clear();
            active_sinks_[sink_param.sink_id] = new_internal_state;
            LOG_CPP_DEBUG("    Sink %s added to AudioManager and internal state.", sink_param.sink_id.c_str());

            // Now reconcile connections for the newly added sink.
            LOG_CPP_DEBUG("    -> Calling reconcile_connections_for_sink for ADDED sink: %s", sink_param.sink_id.c_str());
            reconcile_connections_for_sink(sink_param); // Pass desired state.
        } else {
            LOG_CPP_ERROR("    AudioManager failed to add sink: %s", sink_param.sink_id.c_str());
            // Don't add to internal state or reconcile connections if add failed.
        }
    }
}

/**
 * @brief Processes updates for existing sinks.
 *
 * This can involve re-creating the sink if its core configuration changed, or simply
 * updating its connections to source paths.
 * @param sinks_to_update A vector of parameters for the sinks to be updated.
 */
void AudioEngineConfigApplier::process_sink_updates(const std::vector<AppliedSinkParams>& sinks_to_update) {
    LOG_CPP_DEBUG("Processing %zu sink updates...", sinks_to_update.size());
    for (const auto& desired_sink_param : sinks_to_update) {
        const std::string& sink_id = desired_sink_param.sink_id;
        LOG_CPP_DEBUG("  - Updating sink: %s", sink_id.c_str());

        auto active_it = active_sinks_.find(sink_id);
        if (active_it == active_sinks_.end()) {
            LOG_CPP_ERROR("    Cannot update sink %s: Not found in active state (should not happen).", sink_id.c_str());
            continue;
        }
        InternalSinkState& current_internal_state = active_it->second;

        // Check if core engine parameters changed, requiring a re-creation of the sink.
        bool config_changed = !compare_sink_configs(current_internal_state.params.sink_engine_config, desired_sink_param.sink_engine_config);

        if (config_changed) {
            LOG_CPP_DEBUG("    Core sink parameters changed for %s. Re-adding sink.", sink_id.c_str());
            // Remove the old sink from AudioManager.
            if (!audio_manager_.remove_sink(sink_id)) {
                 LOG_CPP_ERROR("    Failed to remove sink %s during update. Aborting update for this sink.", sink_id.c_str());
                 continue; // Skip to next sink if removal failed.
            }
            // Add the sink back with the new config.
            if (!audio_manager_.add_sink(desired_sink_param.sink_engine_config)) {
                 LOG_CPP_ERROR("    Failed to re-add sink %s with new config during update. Sink is now removed.", sink_id.c_str());
                 active_sinks_.erase(active_it); // Remove from internal state as it couldn't be re-added.
                 continue; // Skip to next sink.
            }
            LOG_CPP_DEBUG("    Sink %s re-added successfully with new config.", sink_id.c_str());
            // Update internal config state.
            current_internal_state.params.sink_engine_config = desired_sink_param.sink_engine_config;
            // Clear internal connection state as they will be re-established.
            current_internal_state.params.connected_source_path_ids.clear();
        }

        // Always reconcile connections for updated sinks (whether config changed or only connections changed).
        LOG_CPP_DEBUG("    -> Calling reconcile_connections_for_sink for UPDATED sink: %s", sink_id.c_str());
        reconcile_connections_for_sink(desired_sink_param); // Pass desired state.
        
        // The internal state's connections are updated inside reconcile_connections_for_sink.
    }
}

// --- Source Path Management ---

/**
 * @brief Compares two AppliedSourcePathParams objects for equality with floating-point tolerance.
 *
 * This function checks if two source path configurations are functionally identical. It is used
 * during reconciliation to determine if a source path's parameters need to be updated.
 *
 * @param a The first AppliedSourcePathParams object.
 * @param b The second AppliedSourcePathParams object.
 * @return True if the parameters are considered equal, false otherwise.
 */
bool compare_applied_source_path_params(const AppliedSourcePathParams& a, const AppliedSourcePathParams& b) {
    const float epsilon = std::numeric_limits<float>::epsilon() * 100; // Tolerance for float comparison.

    bool volume_equal = std::abs(a.volume - b.volume) < epsilon;
    bool timeshift_equal = std::abs(a.timeshift_sec - b.timeshift_sec) < epsilon;

    // Compare the map of speaker layouts.
    // This assumes that CppSpeakerLayout has a well-defined operator==.
    bool layouts_equal = true;
    if (a.speaker_layouts_map.size() != b.speaker_layouts_map.size()) {
        layouts_equal = false;
    } else {
        for (const auto& pair_a : a.speaker_layouts_map) {
            auto it_b = b.speaker_layouts_map.find(pair_a.first);
            if (it_b == b.speaker_layouts_map.end()) {
                layouts_equal = false; // Key from 'a' is missing in 'b'.
                break;
            }
            // Compare the CppSpeakerLayout objects.
            if (!(pair_a.second == it_b->second)) {
                layouts_equal = false; // Layouts for the same key differ.
                break;
            }
        }
    }

    return a.source_tag == b.source_tag &&
           a.target_sink_id == b.target_sink_id &&
           volume_equal &&
           a.eq_values == b.eq_values &&
           a.delay_ms == b.delay_ms &&
           timeshift_equal &&
           a.target_output_channels == b.target_output_channels &&
           a.target_output_samplerate == b.target_output_samplerate &&
           layouts_equal;
}

/**
 * @brief Reconciles the desired source path state with the active state.
 *
 * Compares the list of desired source paths against the currently active ones to determine
 * which paths need to be added, removed, or have their parameters updated.
 *
 * @param desired_source_paths The vector of source paths in the desired configuration.
 * @param[out] path_ids_to_remove Output vector for IDs of paths to be removed.
 * @param[out] paths_to_add Output vector for parameters of paths to be added.
 * @param[out] paths_to_update Output vector for parameters of paths to be updated.
 */
void AudioEngineConfigApplier::reconcile_source_paths(
    const std::vector<AppliedSourcePathParams>& desired_source_paths,
    std::vector<std::string>& path_ids_to_remove,
    std::vector<AppliedSourcePathParams>& paths_to_add,
    std::vector<AppliedSourcePathParams>& paths_to_update)
{
    LOG_CPP_DEBUG("Reconciling source paths...");
    path_ids_to_remove.clear();
    paths_to_add.clear();
    paths_to_update.clear();

    // Create a set of desired path IDs for quick lookup.
    std::set<std::string> desired_path_ids;
    for (const auto& desired_path : desired_source_paths) {
        desired_path_ids.insert(desired_path.path_id);
    }

    // 1. Identify paths to remove.
    for (const auto& pair : active_source_paths_) {
        const std::string& active_path_id = pair.first;
        if (desired_path_ids.find(active_path_id) == desired_path_ids.end()) {
            // Active path not found in desired state, so mark it for removal.
            path_ids_to_remove.push_back(active_path_id);
        }
    }

    // 2. Identify paths to add or update.
    for (const auto& desired_path : desired_source_paths) {
        auto active_it = active_source_paths_.find(desired_path.path_id);
        if (active_it == active_source_paths_.end()) {
            // Desired path not found in active state, so mark it for addition.
            paths_to_add.push_back(desired_path);
        } else {
            // Path exists, check if its parameters have changed.
            const InternalSourcePathState& current_state = active_it->second;
            if (!compare_applied_source_path_params(current_state.params, desired_path)) {
                // Parameters differ, so mark for update.
                paths_to_update.push_back(desired_path);
            }
        }
    }
     LOG_CPP_DEBUG("Source path reconciliation complete. To remove: %zu, To add: %zu, To update: %zu",
                  path_ids_to_remove.size(), paths_to_add.size(), paths_to_update.size());
}

/**
 * @brief Processes the removal of source paths from the audio engine.
 * @param path_ids_to_remove A vector of path IDs to be removed.
 */
void AudioEngineConfigApplier::process_source_path_removals(const std::vector<std::string>& path_ids_to_remove) {
    LOG_CPP_DEBUG("Processing %zu source path removals...", path_ids_to_remove.size());
    for (const auto& path_id : path_ids_to_remove) {
        LOG_CPP_DEBUG("  - Removing path: %s", path_id.c_str());
        auto it = active_source_paths_.find(path_id);
        if (it != active_source_paths_.end()) {
            const std::string& instance_id = it->second.params.generated_instance_id;
            if (!instance_id.empty()) {
                if (audio_manager_.remove_source(instance_id)) {
                    LOG_CPP_DEBUG("    Source instance %s removed successfully from AudioManager.", instance_id.c_str());
                } else {
                    LOG_CPP_ERROR("    AudioManager failed to remove source instance: %s for path: %s", instance_id.c_str(), path_id.c_str());
                }
            } else {
                LOG_CPP_ERROR("    Path %s marked for removal but has no generated_instance_id in active state.", path_id.c_str());
            }
            // Remove from internal state regardless of AudioManager success to avoid repeated attempts.
            active_source_paths_.erase(it);
            LOG_CPP_DEBUG("    Path %s removed from internal state.", path_id.c_str());
        } else {
            LOG_CPP_ERROR("    Path %s marked for removal but not found in active_source_paths_.", path_id.c_str());
        }
    }
}

/**
 * @brief Processes the addition of a new source path to the audio engine.
 *
 * This involves creating a SourceConfig, configuring it in the AudioManager, and storing
 * the resulting instance ID.
 *
 * @param[in,out] path_param_to_add The parameters for the new source path. The `generated_instance_id`
 *                                  field will be populated on success.
 * @return True if the source was added successfully, false otherwise.
 */
bool AudioEngineConfigApplier::process_source_path_addition(AppliedSourcePathParams& path_param_to_add) {
    LOG_CPP_DEBUG("Processing source path addition for path_id: %s", path_param_to_add.path_id.c_str());
    
    // 1. Create C++ SourceConfig from the provided parameters.
    audio::SourceConfig cpp_source_config;
    cpp_source_config.tag = path_param_to_add.source_tag;
    cpp_source_config.initial_volume = path_param_to_add.volume;
    // Ensure EQ values are correctly sized.
    if (path_param_to_add.eq_values.size() != EQ_BANDS) {
         LOG_CPP_ERROR("    EQ size mismatch for path %s. Expected %d, got %zu. Using default flat EQ.",
                       path_param_to_add.path_id.c_str(), EQ_BANDS, path_param_to_add.eq_values.size());
         cpp_source_config.initial_eq.assign(EQ_BANDS, 1.0f);
    } else {
        cpp_source_config.initial_eq = path_param_to_add.eq_values;
    }
    cpp_source_config.initial_delay_ms = path_param_to_add.delay_ms;
    cpp_source_config.target_output_channels = path_param_to_add.target_output_channels;
    cpp_source_config.target_output_samplerate = path_param_to_add.target_output_samplerate;

    // 2. Call AudioManager to configure the source and get an instance ID.
    std::string instance_id = audio_manager_.configure_source(cpp_source_config);

    // 3. Handle the result and update the parameter struct.
    if (instance_id.empty()) {
        LOG_CPP_ERROR("    AudioManager failed to configure source for path_id: %s with source_tag: %s",
                      path_param_to_add.path_id.c_str(), path_param_to_add.source_tag.c_str());
        path_param_to_add.generated_instance_id.clear(); // Ensure ID is empty on failure.
        return false;
    } else {
        LOG_CPP_DEBUG("    Successfully configured source for path_id: %s, got instance_id: %s",
                     path_param_to_add.path_id.c_str(), instance_id.c_str());
        path_param_to_add.generated_instance_id = instance_id; // Store the generated ID.
        
        // Apply the initial speaker layouts map for the newly added source.
        LOG_CPP_DEBUG("    Applying initial speaker_layouts_map for new source instance %s", instance_id.c_str());
        
        audio::SourceParameterUpdates updates;
        updates.speaker_layouts_map = path_param_to_add.speaker_layouts_map;
        audio_manager_.update_source_parameters(instance_id, updates);

        LOG_CPP_DEBUG("    Initial speaker_layouts_map applied for instance %s", instance_id.c_str());

        // The caller (apply_state) is responsible for adding this to the active_source_paths_ map.
        return true;
    }
}

/**
 * @brief Processes updates for existing source paths.
 *
 * This handles changes to parameters like volume, EQ, and delay. If a fundamental
 * property (like the source tag or audio format) changes, it re-creates the source path.
 *
 * @param paths_to_update A vector of parameters for the source paths to be updated.
 */
void AudioEngineConfigApplier::process_source_path_updates(const std::vector<AppliedSourcePathParams>& paths_to_update) {
    LOG_CPP_DEBUG("Processing %zu source path updates...", paths_to_update.size());
    for (const auto& desired_path_param : paths_to_update) {
        const std::string& path_id = desired_path_param.path_id;
        LOG_CPP_DEBUG("  - Updating path: %s", path_id.c_str());

        auto active_it = active_source_paths_.find(path_id);
        if (active_it == active_source_paths_.end()) {
            LOG_CPP_ERROR("    Cannot update path %s: Not found in active state (should not happen).", path_id.c_str());
            continue;
        }
        InternalSourcePathState& current_path_state = active_it->second;
        const std::string& instance_id = current_path_state.params.generated_instance_id;

        if (instance_id.empty()) {
            LOG_CPP_ERROR("    Cannot update path %s: Missing generated_instance_id in active state.", path_id.c_str());
            continue;
        }

        // Check for fundamental changes requiring re-creation of the source processor.
        bool fundamental_change =
            current_path_state.params.source_tag != desired_path_param.source_tag ||
            current_path_state.params.target_output_channels != desired_path_param.target_output_channels ||
            current_path_state.params.target_output_samplerate != desired_path_param.target_output_samplerate;

        if (fundamental_change) {
            LOG_CPP_DEBUG("    Fundamental change detected for path %s. Re-creating SourceInputProcessor.", path_id.c_str());
            // Remove the old instance.
            if (!audio_manager_.remove_source(instance_id)) {
                 LOG_CPP_ERROR("    Failed to remove old source instance %s during update. Aborting update for this path.", instance_id.c_str());
                 continue; // Skip to the next path.
            }
            // Remove from internal state immediately.
            active_source_paths_.erase(active_it);

            // Add a new instance.
            AppliedSourcePathParams temp_param_for_add = desired_path_param;
            if (process_source_path_addition(temp_param_for_add)) {
                // Add the new state back to the internal map.
                active_source_paths_[temp_param_for_add.path_id] = {temp_param_for_add};
                LOG_CPP_DEBUG("    Path %s re-created with new instance_id: %s",
                             path_id.c_str(), temp_param_for_add.generated_instance_id.c_str());
                // Connections for this new instance_id will be re-established by the sink update logic.
            } else {
                LOG_CPP_ERROR("    Failed to re-create source path %s after fundamental change. Path is now removed.", path_id.c_str());
                // Path remains removed from active_source_paths_.
            }
            // Whether re-add succeeded or failed, we are done with this path for this update cycle.
            continue;
        }

        // Process non-fundamental parameter updates.
        LOG_CPP_DEBUG("    Applying parameter updates for path %s (Instance: %s)", path_id.c_str(), instance_id.c_str());

        audio::SourceParameterUpdates updates;
        updates.volume = desired_path_param.volume;
        if (desired_path_param.eq_values.size() == EQ_BANDS) {
            updates.eq_values = desired_path_param.eq_values;
            updates.eq_normalization = desired_path_param.eq_normalization;
        } else {
            LOG_CPP_ERROR("    Invalid EQ size (%zu) for path update %s. Skipping EQ update.",
                          desired_path_param.eq_values.size(), path_id.c_str());
        }
        updates.volume_normalization = desired_path_param.volume_normalization;
        updates.delay_ms = desired_path_param.delay_ms;
        updates.timeshift_sec = desired_path_param.timeshift_sec;
        updates.speaker_layouts_map = desired_path_param.speaker_layouts_map;

        audio_manager_.update_source_parameters(instance_id, updates);

        // Update the internal state to reflect the desired parameters.
        // The generated instance ID must be preserved.
        std::string preserved_instance_id = current_path_state.params.generated_instance_id;
        current_path_state.params = desired_path_param;
        current_path_state.params.generated_instance_id = preserved_instance_id;
        LOG_CPP_DEBUG("    Internal state updated for path %s", path_id.c_str());
    }
}

// --- Connection Management ---

/**
 * @brief Reconciles the connections for a specific sink.
 *
 * Compares the desired connections for a sink with its current connections and instructs
 * the AudioManager to connect or disconnect source paths as needed.
 *
 * @param desired_sink_params The parameters of the sink, including its desired list of
 *                            `connected_source_path_ids`.
 */
void AudioEngineConfigApplier::reconcile_connections_for_sink(const AppliedSinkParams& desired_sink_params) {
    const std::string& sink_id = desired_sink_params.sink_id;
    LOG_CPP_DEBUG("Reconciling connections for sink: %s", sink_id.c_str());

    // 1. Find the current internal state for this sink.
    auto current_sink_state_it = active_sinks_.find(sink_id);
    if (current_sink_state_it == active_sinks_.end()) {
        LOG_CPP_ERROR("    Cannot reconcile connections for unknown sink: %s", sink_id.c_str());
        return; // Should not happen if called correctly from add/update logic.
    }
    InternalSinkState& current_sink_state = current_sink_state_it->second;

    // 2. Get current and desired connection sets for easy comparison.
    const std::vector<std::string>& current_path_ids_vec = current_sink_state.params.connected_source_path_ids;
    std::set<std::string> current_path_ids_set(current_path_ids_vec.begin(), current_path_ids_vec.end());

    const std::vector<std::string>& desired_path_ids_vec = desired_sink_params.connected_source_path_ids;
    std::set<std::string> desired_path_ids_set(desired_path_ids_vec.begin(), desired_path_ids_vec.end());

    // Log current vs. desired connections for debugging.
    LOG_CPP_DEBUG("    Current connection path IDs (%zu):", current_path_ids_set.size());
    if (current_path_ids_set.empty()) { LOG_CPP_DEBUG("      (None)"); }
    for(const auto& id : current_path_ids_set) { LOG_CPP_DEBUG("      - %s", id.c_str()); }
    LOG_CPP_DEBUG("    Desired connection path IDs (%zu):", desired_path_ids_set.size());
    if (desired_path_ids_set.empty()) { LOG_CPP_DEBUG("      (None)"); }
    for(const auto& id : desired_path_ids_set) { LOG_CPP_DEBUG("      - %s", id.c_str()); }

    // 3. Identify and process connections to add.
    LOG_CPP_DEBUG("    Checking connections to add...");
    for (const auto& desired_path_id : desired_path_ids_set) {
        if (current_path_ids_set.find(desired_path_id) == current_path_ids_set.end()) {
            // This connection is in the desired state but not the current state.
            // Look up the source path details to get its instance ID.
            auto source_path_it = active_source_paths_.find(desired_path_id);
            if (source_path_it == active_source_paths_.end() || source_path_it->second.params.generated_instance_id.empty()) {
                LOG_CPP_ERROR("      + Cannot connect path %s to sink %s: Source path or its instance_id not found/generated.",
                              desired_path_id.c_str(), sink_id.c_str());
                continue; // Skip this connection.
            }
            const AppliedSourcePathParams& source_params = source_path_it->second.params;
            const std::string& source_instance_id = source_params.generated_instance_id;
            const audio::SinkConfig& sink_config = desired_sink_params.sink_engine_config;

            LOG_CPP_DEBUG("      + Connecting Source:");
            LOG_CPP_DEBUG("          Path ID: %s", desired_path_id.c_str());
            LOG_CPP_DEBUG("          Instance ID: %s", source_instance_id.c_str());
            LOG_CPP_DEBUG("          Source Tag: %s", source_params.source_tag.c_str());
            LOG_CPP_DEBUG("        To Sink:");
            LOG_CPP_DEBUG("          Sink ID: %s", sink_id.c_str());
            LOG_CPP_DEBUG("          Target: %s:%d", sink_config.output_ip.c_str(), sink_config.output_port);
            LOG_CPP_DEBUG("          Format: %dch@%dHz, %dbit", sink_config.channels, sink_config.samplerate, sink_config.bitdepth);

            // Call AudioManager to establish the connection.
            if (!audio_manager_.connect_source_sink(source_instance_id, sink_id)) {
                 LOG_CPP_ERROR("        -> AudioManager connect_source_sink FAILED.");
            } else {
                 LOG_CPP_DEBUG("        -> Connection successful.");
            }
        }
    }

    // 4. Identify and process connections to remove.
    LOG_CPP_DEBUG("    Checking connections to remove...");
    for (const auto& current_path_id : current_path_ids_set) {
        if (desired_path_ids_set.find(current_path_id) == desired_path_ids_set.end()) {
            // This connection is in the current state but not the desired state.
            // Look up the source path details (it might have been removed already, so handle gracefully).
            auto source_path_it = active_source_paths_.find(current_path_id);
            std::string source_instance_id = "UNKNOWN (Path Removed?)";
            std::string source_tag = "UNKNOWN";
            if (source_path_it != active_source_paths_.end()) {
                 source_instance_id = source_path_it->second.params.generated_instance_id;
                 source_tag = source_path_it->second.params.source_tag;
            } else {
                 LOG_CPP_ERROR("      - Cannot find source path details for path %s during disconnection (might have been removed already). Attempting disconnect anyway.",
                               current_path_id.c_str());
            }
             
            LOG_CPP_DEBUG("      - Disconnecting Source:");
            LOG_CPP_DEBUG("          Path ID: %s", current_path_id.c_str());
            LOG_CPP_DEBUG("          Instance ID: %s", source_instance_id.c_str());
            LOG_CPP_DEBUG("          Source Tag: %s", source_tag.c_str());
            LOG_CPP_DEBUG("        From Sink:");
            LOG_CPP_DEBUG("          Sink ID: %s", sink_id.c_str());

            // Call AudioManager to break the connection.
            if (!audio_manager_.disconnect_source_sink(source_instance_id, sink_id)) {
                 // Log an error, but failure might be expected if the source was already removed by the AudioManager.
                 LOG_CPP_ERROR("        -> AudioManager disconnect_source_sink FAILED (might be expected if source was already removed).");
            } else {
                 LOG_CPP_DEBUG("        -> Disconnection successful.");
            }
        }
    }

    // 5. Update the internal state to match the desired state.
    current_sink_state.params.connected_source_path_ids = desired_sink_params.connected_source_path_ids;
    LOG_CPP_DEBUG("    Internal connection state updated for sink %s", sink_id.c_str());
}


} // namespace config
} // namespace screamrouter
