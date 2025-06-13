// src/configuration/audio_engine_config_applier.cpp
#include "audio_engine_config_applier.h"
#include "audio_manager.h" // Include full header for AudioManager methods
#include <iostream> // Temporary for logging
#include <algorithm> // For std::find_if, std::find
#include <vector>
#include <string>
#include <map>
#include <set>
#include <cmath> // For std::abs
#include <limits> // For std::numeric_limits
 
// Define a simple logger macro for now, replace with proper logger later
#define LOG_APPLIER(msg) std::cout << "[ConfigApplier] " << msg << std::endl
#define LOG_APPLIER_ERROR(msg) std::cerr << "[ConfigApplier ERROR] " << msg << std::endl

namespace screamrouter {
namespace config {

// --- Constructor & Destructor ---

AudioEngineConfigApplier::AudioEngineConfigApplier(audio::AudioManager& audio_manager)
    : audio_manager_(audio_manager) {
    LOG_APPLIER("AudioEngineConfigApplier created.");
    // active_source_paths_ and active_sinks_ are default initialized (empty)
}

AudioEngineConfigApplier::~AudioEngineConfigApplier() {
    LOG_APPLIER("AudioEngineConfigApplier destroyed.");
    // No ownership of audio_manager_, so no deletion needed.
}

// --- Public Methods ---

bool AudioEngineConfigApplier::apply_state(const DesiredEngineState& desired_state) {
    LOG_APPLIER("Applying new engine state...");

    std::vector<std::string> sink_ids_to_remove;
    std::vector<AppliedSinkParams> sinks_to_add;
    std::vector<AppliedSinkParams> sinks_to_update;

    std::vector<std::string> path_ids_to_remove;
    std::vector<AppliedSourcePathParams> paths_to_add; // Will hold paths that need new SourceInputProcessors
    std::vector<AppliedSourcePathParams> paths_to_update; // Paths whose parameters (vol, EQ) changed

    // 1. Reconcile to find differences between desired state and current active state
    LOG_APPLIER("Reconciling sinks...");
    reconcile_sinks(desired_state.sinks, sink_ids_to_remove, sinks_to_add, sinks_to_update);
    LOG_APPLIER("Reconciling source paths...");
    reconcile_source_paths(desired_state.source_paths, path_ids_to_remove, paths_to_add, paths_to_update);

    // 2. Process removals first (order matters: remove dependencies before dependents if necessary)
    // Order: Remove source paths first, then sinks. AudioManager handles internal disconnections.
    LOG_APPLIER("Processing source path removals (" << path_ids_to_remove.size() << ")...");
    process_source_path_removals(path_ids_to_remove);

    LOG_APPLIER("Processing sink removals (" << sink_ids_to_remove.size() << ")...");
    process_sink_removals(sink_ids_to_remove);

    // 3. Process additions
    LOG_APPLIER("Processing source path additions (" << paths_to_add.size() << ")...");
    // Create a temporary map to store newly added paths with their generated IDs for connection lookup
    std::map<std::string, std::string> added_path_id_to_instance_id; 
    for (auto& path_param : paths_to_add) { // Pass by reference to update generated_instance_id
        if (process_source_path_addition(path_param)) {
            // Successfully added, update our active state map immediately
            active_source_paths_[path_param.path_id] = {path_param}; 
            // Store the generated ID for connection phase
            added_path_id_to_instance_id[path_param.path_id] = path_param.generated_instance_id;
        } else {
            LOG_APPLIER_ERROR("Failed to add source path: " + path_param.path_id + ". Skipping associated connections.");
            // Handle error - potentially stop further processing or mark overall failure? For now, just log.
        }
    }

    LOG_APPLIER("Processing sink additions (" << sinks_to_add.size() << ")...");
    // Sink additions will also handle initial connections using reconcile_connections_for_sink
    process_sink_additions(sinks_to_add); 

    // 4. Process updates (parameter changes for existing items, connection changes for existing sinks)
    LOG_APPLIER("Processing source path updates (" << paths_to_update.size() << ")...");
    process_source_path_updates(paths_to_update);
    
    LOG_APPLIER("Processing sink updates (connections) (" << sinks_to_update.size() << ")...");
    // Sink updates will primarily handle connection changes via reconcile_connections_for_sink
    process_sink_updates(sinks_to_update); 

    LOG_APPLIER("Engine state application finished.");
    // TODO: Implement proper success/failure tracking based on helper method results
    return true; // Placeholder
}

// --- Helper Function for SinkConfig Comparison ---
// Returns true if configs are considered equal for reconciliation purposes
bool compare_sink_configs(const audio::SinkConfig& a, const audio::SinkConfig& b) {
    // Compare all relevant fields that would require a sink re-creation if changed
    return a.id == b.id && // ID should match if comparing the same conceptual sink
           a.output_ip == b.output_ip &&
           a.output_port == b.output_port &&
           a.bitdepth == b.bitdepth &&
           a.samplerate == b.samplerate &&
           a.channels == b.channels &&
           a.chlayout1 == b.chlayout1 &&
           a.chlayout2 == b.chlayout2 &&
           a.enable_mp3 == b.enable_mp3 && // Include any other relevant fields
           a.protocol == b.protocol;
}

// --- Helper Function for Connection List Comparison ---
// Returns true if the connected path IDs are the same (order doesn't matter)
bool compare_connections(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    // Convert to sets for efficient comparison
    std::set<std::string> set_a(a.begin(), a.end());
    std::set<std::string> set_b(b.begin(), b.end());
    return set_a == set_b;
}


// --- Method Implementations ---

// --- Sink Reconciliation (Task 02_03) ---

void AudioEngineConfigApplier::reconcile_sinks(
    const std::vector<AppliedSinkParams>& desired_sinks,
    std::vector<std::string>& sink_ids_to_remove,
    std::vector<AppliedSinkParams>& sinks_to_add,
    std::vector<AppliedSinkParams>& sinks_to_update)
{
    LOG_APPLIER("Reconciling sinks...");
    sink_ids_to_remove.clear();
    sinks_to_add.clear();
    sinks_to_update.clear();

    // Create a set of desired sink IDs for quick lookup
    std::set<std::string> desired_sink_ids;
    for (const auto& desired_sink : desired_sinks) {
        desired_sink_ids.insert(desired_sink.sink_id);
    }

    // 1. Identify sinks to remove
    for (const auto& pair : active_sinks_) {
        const std::string& active_sink_id = pair.first;
        if (desired_sink_ids.find(active_sink_id) == desired_sink_ids.end()) {
            // Active sink not found in desired state -> mark for removal
            sink_ids_to_remove.push_back(active_sink_id);
        }
    }

    // 2. Identify sinks to add or update
    for (const auto& desired_sink : desired_sinks) {
        auto active_it = active_sinks_.find(desired_sink.sink_id);
        if (active_it == active_sinks_.end()) {
            // Desired sink not found in active state -> mark for addition
            sinks_to_add.push_back(desired_sink);
        } else {
            // Sink exists, check if update is needed
            const InternalSinkState& current_state = active_it->second;
            bool config_changed = !compare_sink_configs(current_state.params.sink_engine_config, desired_sink.sink_engine_config);
            bool connections_changed = !compare_connections(current_state.params.connected_source_path_ids, desired_sink.connected_source_path_ids);

            if (config_changed || connections_changed) {
                sinks_to_update.push_back(desired_sink);
            }
        }
    }
    LOG_APPLIER("Sink reconciliation complete. To remove: " << sink_ids_to_remove.size() 
              << ", To add: " << sinks_to_add.size() 
              << ", To update: " << sinks_to_update.size());
}

void AudioEngineConfigApplier::process_sink_removals(const std::vector<std::string>& sink_ids_to_remove) {
    LOG_APPLIER("Processing " << sink_ids_to_remove.size() << " sink removals...");
    for (const auto& sink_id : sink_ids_to_remove) {
        LOG_APPLIER("  - Removing sink: " + sink_id);
        if (audio_manager_.remove_sink(sink_id)) {
            active_sinks_.erase(sink_id);
            LOG_APPLIER("    Sink " + sink_id + " removed successfully from AudioManager and internal state.");
        } else {
            LOG_APPLIER_ERROR("    AudioManager failed to remove sink: " + sink_id + ". Internal state may be inconsistent.");
            // Attempt to remove from internal state anyway to avoid repeated attempts
            active_sinks_.erase(sink_id);
        }
    }
}

void AudioEngineConfigApplier::process_sink_additions(const std::vector<AppliedSinkParams>& sinks_to_add) {
    LOG_APPLIER("Processing " << sinks_to_add.size() << " sink additions...");
    for (const auto& sink_param : sinks_to_add) {
        LOG_APPLIER("  - Adding sink: " + sink_param.sink_id);
        if (audio_manager_.add_sink(sink_param.sink_engine_config)) {
            // Add to internal state
            InternalSinkState new_internal_state;
            new_internal_state.params = sink_param; // Store desired params
            // Crucially, clear connections initially; reconcile_connections will set them.
            new_internal_state.params.connected_source_path_ids.clear(); 
            active_sinks_[sink_param.sink_id] = new_internal_state;
            LOG_APPLIER("    Sink " + sink_param.sink_id + " added to AudioManager and internal state.");

            // Now reconcile connections for the newly added sink
            LOG_APPLIER("    -> Calling reconcile_connections_for_sink for ADDED sink: " + sink_param.sink_id); // Added log
            reconcile_connections_for_sink(sink_param); // Pass desired state
        } else {
            LOG_APPLIER_ERROR("    AudioManager failed to add sink: " + sink_param.sink_id);
            // Don't add to internal state or reconcile connections if add failed
        }
    }
}

void AudioEngineConfigApplier::process_sink_updates(const std::vector<AppliedSinkParams>& sinks_to_update) {
    LOG_APPLIER("Processing " << sinks_to_update.size() << " sink updates...");
    for (const auto& desired_sink_param : sinks_to_update) {
        const std::string& sink_id = desired_sink_param.sink_id;
        LOG_APPLIER("  - Updating sink: " + sink_id);

        auto active_it = active_sinks_.find(sink_id);
        if (active_it == active_sinks_.end()) {
            LOG_APPLIER_ERROR("    Cannot update sink " + sink_id + ": Not found in active state (should not happen).");
            continue;
        }
        InternalSinkState& current_internal_state = active_it->second;

        // Check if core engine parameters changed
        bool config_changed = !compare_sink_configs(current_internal_state.params.sink_engine_config, desired_sink_param.sink_engine_config);

        if (config_changed) {
            LOG_APPLIER("    Core sink parameters changed for " + sink_id + ". Re-adding sink.");
            // Remove the old sink from AudioManager
            if (!audio_manager_.remove_sink(sink_id)) {
                 LOG_APPLIER_ERROR("    Failed to remove sink " + sink_id + " during update. Aborting update for this sink.");
                 continue; // Skip to next sink if removal failed
            }
            // Add the sink back with the new config
            if (!audio_manager_.add_sink(desired_sink_param.sink_engine_config)) {
                 LOG_APPLIER_ERROR("    Failed to re-add sink " + sink_id + " with new config during update. Sink is now removed.");
                 active_sinks_.erase(active_it); // Remove from internal state as it couldn't be re-added
                 continue; // Skip to next sink
            }
            LOG_APPLIER("    Sink " + sink_id + " re-added successfully with new config.");
            // Update internal config state
            current_internal_state.params.sink_engine_config = desired_sink_param.sink_engine_config;
            // Clear internal connection state as they will be re-established
            current_internal_state.params.connected_source_path_ids.clear(); 
        }

        // Always reconcile connections for updated sinks (either config changed or only connections changed)
        LOG_APPLIER("    -> Calling reconcile_connections_for_sink for UPDATED sink: " + sink_id); // Added log
        reconcile_connections_for_sink(desired_sink_param); // Pass desired state
        
        // After reconcile_connections_for_sink, the internal state's connections should match desired.
        // This update happens inside reconcile_connections_for_sink now.
    }
}

// --- Source Path Reconciliation (Task 02_04) ---

// Helper Function for AppliedSourcePathParams Comparison with tolerance
bool compare_applied_source_path_params(const AppliedSourcePathParams& a, const AppliedSourcePathParams& b) {
    const float epsilon = std::numeric_limits<float>::epsilon() * 100; // Tolerance for float comparison

    bool volume_equal = std::abs(a.volume - b.volume) < epsilon;
    bool timeshift_equal = std::abs(a.timeshift_sec - b.timeshift_sec) < epsilon;

    // Compare speaker_layouts_map
    // This requires comparing maps of CppSpeakerLayout objects.
    // CppSpeakerLayout itself needs an equality operator or a comparison function.
    // For now, a simple size check and element-wise comparison if CppSpeakerLayout is comparable.
    // Assuming CppSpeakerLayout has an operator== defined or can be compared field by field.
    bool layouts_equal = true;
    if (a.speaker_layouts_map.size() != b.speaker_layouts_map.size()) {
        layouts_equal = false;
    } else {
        for (const auto& pair_a : a.speaker_layouts_map) {
            auto it_b = b.speaker_layouts_map.find(pair_a.first);
            if (it_b == b.speaker_layouts_map.end()) {
                layouts_equal = false; // Key missing in b
                break;
            }
            // Assuming CppSpeakerLayout has operator==
            // If not, this needs to be:
            // if (!(pair_a.second.auto_mode == it_b->second.auto_mode && pair_a.second.matrix == it_b->second.matrix))
            if (!(pair_a.second == it_b->second)) { 
                layouts_equal = false; // Layouts for the same key differ
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
           layouts_equal; // Added speaker_layouts_map comparison
}

void AudioEngineConfigApplier::reconcile_source_paths(
    const std::vector<AppliedSourcePathParams>& desired_source_paths,
    std::vector<std::string>& path_ids_to_remove,
    std::vector<AppliedSourcePathParams>& paths_to_add,
    std::vector<AppliedSourcePathParams>& paths_to_update) 
{
    LOG_APPLIER("Reconciling source paths...");
    path_ids_to_remove.clear();
    paths_to_add.clear();
    paths_to_update.clear();

    // Create a set of desired path IDs for quick lookup
    std::set<std::string> desired_path_ids;
    for (const auto& desired_path : desired_source_paths) {
        desired_path_ids.insert(desired_path.path_id);
    }

    // 1. Identify paths to remove
    for (const auto& pair : active_source_paths_) {
        const std::string& active_path_id = pair.first;
        if (desired_path_ids.find(active_path_id) == desired_path_ids.end()) {
            // Active path not found in desired state -> mark for removal
            path_ids_to_remove.push_back(active_path_id);
        }
    }

    // 2. Identify paths to add or update
    for (const auto& desired_path : desired_source_paths) {
        auto active_it = active_source_paths_.find(desired_path.path_id);
        if (active_it == active_source_paths_.end()) {
            // Desired path not found in active state -> mark for addition
            paths_to_add.push_back(desired_path);
        } else {
            // Path exists, check if update is needed
            const InternalSourcePathState& current_state = active_it->second;
            if (!compare_applied_source_path_params(current_state.params, desired_path)) {
                // Parameters differ -> mark for update
                paths_to_update.push_back(desired_path);
            }
        }
    }
     LOG_APPLIER("Source path reconciliation complete. To remove: " << path_ids_to_remove.size() 
              << ", To add: " << paths_to_add.size() 
              << ", To update: " << paths_to_update.size());
}

void AudioEngineConfigApplier::process_source_path_removals(const std::vector<std::string>& path_ids_to_remove) {
    LOG_APPLIER("Processing " << path_ids_to_remove.size() << " source path removals...");
    for (const auto& path_id : path_ids_to_remove) {
        LOG_APPLIER("  - Removing path: " + path_id);
        auto it = active_source_paths_.find(path_id);
        if (it != active_source_paths_.end()) {
            const std::string& instance_id = it->second.params.generated_instance_id;
            if (!instance_id.empty()) {
                if (audio_manager_.remove_source(instance_id)) {
                    LOG_APPLIER("    Source instance " + instance_id + " removed successfully from AudioManager.");
                } else {
                    LOG_APPLIER_ERROR("    AudioManager failed to remove source instance: " + instance_id + " for path: " + path_id);
                }
            } else {
                LOG_APPLIER_ERROR("    Path " + path_id + " marked for removal but has no generated_instance_id in active state.");
            }
            // Remove from internal state regardless of AudioManager success to avoid repeated attempts
            active_source_paths_.erase(it);
            LOG_APPLIER("    Path " + path_id + " removed from internal state.");
        } else {
            LOG_APPLIER_ERROR("    Path " + path_id + " marked for removal but not found in active_source_paths_.");
        }
    }
}

bool AudioEngineConfigApplier::process_source_path_addition(AppliedSourcePathParams& path_param_to_add) {
    LOG_APPLIER("Processing source path addition for path_id: " + path_param_to_add.path_id);
    
    // 1. Create C++ SourceConfig from AppliedSourcePathParams
    audio::SourceConfig cpp_source_config;
    cpp_source_config.tag = path_param_to_add.source_tag;
    cpp_source_config.initial_volume = path_param_to_add.volume;
    // Ensure EQ values are correctly sized (constructor should handle default, but double-check)
    if (path_param_to_add.eq_values.size() != EQ_BANDS) {
         LOG_APPLIER_ERROR("    EQ size mismatch for path " + path_param_to_add.path_id + ". Expected " + std::to_string(EQ_BANDS) + ", got " + std::to_string(path_param_to_add.eq_values.size()) + ". Using default flat EQ.");
         cpp_source_config.initial_eq.assign(EQ_BANDS, 1.0f);
    } else {
        cpp_source_config.initial_eq = path_param_to_add.eq_values;
    }
    cpp_source_config.initial_delay_ms = path_param_to_add.delay_ms;
    cpp_source_config.target_output_channels = path_param_to_add.target_output_channels;
    cpp_source_config.target_output_samplerate = path_param_to_add.target_output_samplerate;

    // 2. Call AudioManager to configure the source
    std::string instance_id = audio_manager_.configure_source(cpp_source_config);

    // 3. Handle result and update internal state
    if (instance_id.empty()) {
        LOG_APPLIER_ERROR("    AudioManager failed to configure source for path_id: " + path_param_to_add.path_id + " with source_tag: " + path_param_to_add.source_tag);
        path_param_to_add.generated_instance_id.clear(); // Ensure ID is empty on failure
        return false;
    } else {
        LOG_APPLIER("    Successfully configured source for path_id: " + path_param_to_add.path_id + ", got instance_id: " + instance_id);
        path_param_to_add.generated_instance_id = instance_id; // Store the generated ID
        
        // --- New: Apply Speaker Layouts Map for newly added source ---
        LOG_APPLIER("    Applying initial speaker_layouts_map for new source instance " + instance_id);
        // This assumes AudioManager will have a method to pass the map to the SourceInputProcessor
        // The CppSpeakerLayout struct is defined in audio_engine_config_types.h
        // The path_param_to_add.speaker_layouts_map is std::map<int, CppSpeakerLayout>
        if (!audio_manager_.update_source_speaker_layouts_map(instance_id, path_param_to_add.speaker_layouts_map)) {
            LOG_APPLIER_ERROR("    AudioManager failed to apply initial speaker_layouts_map for instance " + instance_id);
        } else {
            LOG_APPLIER("    Initial speaker_layouts_map applied for instance " + instance_id);
        }
        // --- End New ---

        // The caller (apply_state) is responsible for adding this to active_source_paths_ map
        return true;
    }
}

void AudioEngineConfigApplier::process_source_path_updates(const std::vector<AppliedSourcePathParams>& paths_to_update) {
    LOG_APPLIER("Processing " << paths_to_update.size() << " source path updates...");
    for (const auto& desired_path_param : paths_to_update) {
        const std::string& path_id = desired_path_param.path_id;
        LOG_APPLIER("  - Updating path: " + path_id);

        auto active_it = active_source_paths_.find(path_id);
        if (active_it == active_source_paths_.end()) {
            LOG_APPLIER_ERROR("    Cannot update path " + path_id + ": Not found in active state (should not happen).");
            continue;
        }
        InternalSourcePathState& current_path_state = active_it->second;
        const std::string& instance_id = current_path_state.params.generated_instance_id;

        if (instance_id.empty()) {
            LOG_APPLIER_ERROR("    Cannot update path " + path_id + ": Missing generated_instance_id in active state.");
            continue;
        }

        // Check for fundamental changes requiring re-creation
        bool fundamental_change = 
            current_path_state.params.source_tag != desired_path_param.source_tag ||
            current_path_state.params.target_output_channels != desired_path_param.target_output_channels ||
            current_path_state.params.target_output_samplerate != desired_path_param.target_output_samplerate;

        if (fundamental_change) {
            LOG_APPLIER("    Fundamental change detected for path " + path_id + ". Re-creating SourceInputProcessor.");
            // Remove old instance
            if (!audio_manager_.remove_source(instance_id)) {
                 LOG_APPLIER_ERROR("    Failed to remove old source instance " + instance_id + " during update. Aborting update for this path.");
                 continue; // Skip to next path
            }
            // Remove from internal state immediately
            active_source_paths_.erase(active_it); 

            // Add new instance (make a mutable copy for process_source_path_addition)
            AppliedSourcePathParams temp_param_for_add = desired_path_param; 
            if (process_source_path_addition(temp_param_for_add)) {
                // Add new state back to internal map
                active_source_paths_[temp_param_for_add.path_id] = {temp_param_for_add}; 
                LOG_APPLIER("    Path " + path_id + " re-created with new instance_id: " + temp_param_for_add.generated_instance_id);
                // Connections for this new instance_id will need to be re-established by the sink update/connection logic later.
            } else {
                LOG_APPLIER_ERROR("    Failed to re-create source path " + path_id + " after fundamental change. Path is now removed.");
                // Path remains removed from active_source_paths_
            }
            // Whether re-add succeeded or failed, we are done with this path for this update cycle.
            continue; 
        }

        // Process non-fundamental parameter updates.
        // Since this path is already marked for update by reconcile_source_paths,
        // unconditionally send update commands to AudioManager for all parameters.
        // AudioManager/SourceInputProcessor can handle redundant commands if needed.
        LOG_APPLIER("    Applying parameter updates for path " + path_id + " (Instance: " + instance_id + ")");
        bool updated = false; // Track if any AudioManager call succeeded for logging
        
        // Volume
        if(audio_manager_.update_source_volume(instance_id, desired_path_param.volume)) updated = true;
        
        // Equalizer
        if (desired_path_param.eq_values.size() == EQ_BANDS) { // Validate size before sending
             if(audio_manager_.update_source_equalizer(instance_id, desired_path_param.eq_values)) updated = true;
         } else {
              LOG_APPLIER_ERROR("    Invalid EQ size (" + std::to_string(desired_path_param.eq_values.size()) + ") for path update " + path_id + ". Skipping EQ update.");
         }
        
        // Delay
        if(audio_manager_.update_source_delay(instance_id, desired_path_param.delay_ms)) updated = true;
        
        // Timeshift
        if(audio_manager_.update_source_timeshift(instance_id, desired_path_param.timeshift_sec)) updated = true;

        // --- New: Update Speaker Layouts Map via AudioManager ---
        LOG_APPLIER("    Applying speaker_layouts_map update for instance " + instance_id);
        if (!audio_manager_.update_source_speaker_layouts_map(instance_id, desired_path_param.speaker_layouts_map)) {
            LOG_APPLIER_ERROR("    AudioManager failed to apply speaker_layouts_map update for instance " + instance_id);
        } else {
            updated = true; // Mark as updated if speaker layouts map was successfully sent
            LOG_APPLIER("    Speaker_layouts_map update sent for instance " + instance_id);
        }
        // --- End New ---

        // Update the internal state to reflect the desired parameters
        if (updated) {
             // Logged above now
        } else {
             // This log might be misleading if only speaker_mix failed but others succeeded.
             // However, if all specific updates log their own failures, this can be a general summary.
             LOG_APPLIER_ERROR("    AudioManager failed to apply some parameter updates for path " + path_id);
        }
        // Update internal state, preserving the generated instance ID
        std::string preserved_instance_id = current_path_state.params.generated_instance_id; // Save the ID
        current_path_state.params = desired_path_param; // Assign the new desired params (volume, eq, etc.)
        current_path_state.params.generated_instance_id = preserved_instance_id; // Restore the saved ID
        LOG_APPLIER("    Internal state updated for path " + path_id);
    }
}

// --- Connection Reconciliation (Task 02_05) ---

void AudioEngineConfigApplier::reconcile_connections_for_sink(const AppliedSinkParams& desired_sink_params) {
    const std::string& sink_id = desired_sink_params.sink_id;
    LOG_APPLIER("Reconciling connections for sink: " + sink_id);

    // 1. Find the current internal state for this sink
    auto current_sink_state_it = active_sinks_.find(sink_id);
    if (current_sink_state_it == active_sinks_.end()) {
        LOG_APPLIER_ERROR("    Cannot reconcile connections for unknown sink: " + sink_id);
        return; // Should not happen if called correctly
    }
    InternalSinkState& current_sink_state = current_sink_state_it->second;

    // 2. Get current and desired connection sets
    const std::vector<std::string>& current_path_ids_vec = current_sink_state.params.connected_source_path_ids;
    std::set<std::string> current_path_ids_set(current_path_ids_vec.begin(), current_path_ids_vec.end());

    const std::vector<std::string>& desired_path_ids_vec = desired_sink_params.connected_source_path_ids;
    std::set<std::string> desired_path_ids_set(desired_path_ids_vec.begin(), desired_path_ids_vec.end());

    // --- Add explicit logging of current vs desired connections ---
    LOG_APPLIER("    Current connection path IDs (" << current_path_ids_set.size() << "):");
    if (current_path_ids_set.empty()) { LOG_APPLIER("      (None)"); }
    for(const auto& id : current_path_ids_set) { LOG_APPLIER("      - " + id); }
    LOG_APPLIER("    Desired connection path IDs (" << desired_path_ids_set.size() << "):");
    if (desired_path_ids_set.empty()) { LOG_APPLIER("      (None)"); }
    for(const auto& id : desired_path_ids_set) { LOG_APPLIER("      - " + id); }
    // --- End added logging ---

    // 3. Identify and process connections to add
    LOG_APPLIER("    Checking connections to add...");
    for (const auto& desired_path_id : desired_path_ids_set) {
        if (current_path_ids_set.find(desired_path_id) == current_path_ids_set.end()) {
            // Connection needs to be added
            // Look up the source path details
            auto source_path_it = active_source_paths_.find(desired_path_id);
            if (source_path_it == active_source_paths_.end() || source_path_it->second.params.generated_instance_id.empty()) {
                LOG_APPLIER_ERROR("      + Cannot connect path " + desired_path_id + " to sink " + sink_id + ": Source path or its instance_id not found/generated.");
                continue; // Skip this connection
            }
            const AppliedSourcePathParams& source_params = source_path_it->second.params;
            const std::string& source_instance_id = source_params.generated_instance_id;
            const audio::SinkConfig& sink_config = desired_sink_params.sink_engine_config; // Use desired sink config for logging details

            LOG_APPLIER("      + Connecting Source:");
            LOG_APPLIER("          Path ID: " + desired_path_id);
            LOG_APPLIER("          Instance ID: " + source_instance_id);
            LOG_APPLIER("          Source Tag: " + source_params.source_tag);
            LOG_APPLIER("        To Sink:");
            LOG_APPLIER("          Sink ID: " + sink_id);
            LOG_APPLIER("          Target: " + sink_config.output_ip + ":" + std::to_string(sink_config.output_port));
            LOG_APPLIER("          Format: " + std::to_string(sink_config.channels) + "ch@" + std::to_string(sink_config.samplerate) + "Hz, " + std::to_string(sink_config.bitdepth) + "bit");

            // Call AudioManager to connect
            if (!audio_manager_.connect_source_sink(source_instance_id, sink_id)) {
                 LOG_APPLIER_ERROR("        -> AudioManager connect_source_sink FAILED.");
            } else {
                 LOG_APPLIER("        -> Connection successful.");
            }
        }
    }

    // 4. Identify and process connections to remove
    LOG_APPLIER("    Checking connections to remove...");
    for (const auto& current_path_id : current_path_ids_set) {
        if (desired_path_ids_set.find(current_path_id) == desired_path_ids_set.end()) {
            // Connection needs to be removed
            // Look up the source path details (it might have been removed already, handle gracefully)
            auto source_path_it = active_source_paths_.find(current_path_id);
            std::string source_instance_id = "UNKNOWN (Path Removed?)";
            std::string source_tag = "UNKNOWN";
            if (source_path_it != active_source_paths_.end()) {
                 source_instance_id = source_path_it->second.params.generated_instance_id;
                 source_tag = source_path_it->second.params.source_tag;
            } else {
                 LOG_APPLIER_ERROR("      - Cannot find source path details for path " + current_path_id + " during disconnection (might have been removed already). Attempting disconnect anyway.");
            }
             
            LOG_APPLIER("      - Disconnecting Source:");
            LOG_APPLIER("          Path ID: " + current_path_id);
            LOG_APPLIER("          Instance ID: " + source_instance_id);
            LOG_APPLIER("          Source Tag: " + source_tag);
            LOG_APPLIER("        From Sink:");
            LOG_APPLIER("          Sink ID: " + sink_id);

            // Call AudioManager to disconnect, even if instance_id is unknown (AudioManager might handle it)
            if (!audio_manager_.disconnect_source_sink(source_instance_id, sink_id)) {
                 // Log error, but failure might be expected if source was already removed by AudioManager
                 LOG_APPLIER_ERROR("        -> AudioManager disconnect_source_sink FAILED (might be expected if source was already removed).");
            } else {
                 LOG_APPLIER("        -> Disconnection successful.");
            }
        }
    }

    // 5. Update internal state to match desired state
    // Use the vector from desired_sink_params directly
    current_sink_state.params.connected_source_path_ids = desired_sink_params.connected_source_path_ids;
    LOG_APPLIER("    Internal connection state updated for sink " + sink_id);
}


} // namespace config
} // namespace screamrouter
