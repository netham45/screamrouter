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
#include <cctype>
#include <sstream>

#include <map>
#include <set>
#include <unordered_map>
#include <cmath> // For std::abs
#include <limits> // For std::numeric_limits
#include <pybind11/pybind11.h>
#include <chrono>
#include <functional>

using namespace screamrouter::audio;

namespace screamrouter {
namespace config {

namespace {
std::string sanitize_screamrouter_label(const std::string& label) {
    std::string out;
    out.reserve(label.size());
    for (char c : label) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else {
            out.push_back('_');
        }
    }
    return out;
}

std::string sanitize_clone_suffix(const std::string& tag) {
    std::string out;
    out.reserve(tag.size());
    for (char c : tag) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else {
            out.push_back('_');
        }
    }
    return out;
}
}


// --- Constructor & Destructor ---

/**
 * @brief Constructs an AudioEngineConfigApplier.
 * @param audio_manager A reference to the AudioManager instance to be controlled.
 */
AudioEngineConfigApplier::AudioEngineConfigApplier(audio::AudioManager& audio_manager)
    : audio_manager_(audio_manager) {
    LOG_CPP_DEBUG("AudioEngineConfigApplier created.");
    audio_manager_.set_stream_tag_listener(
        [this](const std::string& wildcard, const std::string& concrete) {
            this->handle_stream_tag_resolved(wildcard, concrete);
        },
        [this](const std::string& wildcard) {
            this->handle_stream_tag_removed(wildcard);
        });
    // The active_source_paths_ and active_sinks_ maps are default-initialized to be empty.
}

/**
 * @brief Destroys the AudioEngineConfigApplier.
 */
AudioEngineConfigApplier::~AudioEngineConfigApplier() {
    audio_manager_.clear_stream_tag_listener();
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
    std::lock_guard<std::recursive_mutex> lock(apply_mutex_);
    cached_desired_state_ = desired_state;
    DesiredEngineState effective_state = build_effective_state(desired_state);
    cached_desired_state_valid_ = true;
    using clock = std::chrono::steady_clock;
    const auto t_start = clock::now();

    LOG_CPP_INFO("[ConfigApplier] Applying desired state: sinks=%zu, paths=%zu (expanded paths=%zu)",
                 desired_state.sinks.size(), desired_state.source_paths.size(), effective_state.source_paths.size());

    std::vector<std::string> sink_ids_to_remove;
    std::vector<AppliedSinkParams> sinks_to_add;
    std::vector<AppliedSinkParams> sinks_to_update;

    std::vector<std::string> path_ids_to_remove;
    std::vector<AppliedSourcePathParams> paths_to_add;
    std::vector<AppliedSourcePathParams> paths_to_update;

    // 1) Reconcile current vs desired
    const auto t_rec_start = clock::now();
    reconcile_sinks(effective_state.sinks, sink_ids_to_remove, sinks_to_add, sinks_to_update);
    reconcile_source_paths(effective_state.source_paths, path_ids_to_remove, paths_to_add, paths_to_update);
    const auto t_rec_end = clock::now();
    LOG_CPP_INFO("[ConfigApplier] Reconcile: %lld ms | sinks(-%zu +%zu ~%zu) paths(-%zu +%zu ~%zu)",
                 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_rec_end - t_rec_start).count(),
                 sink_ids_to_remove.size(), sinks_to_add.size(), sinks_to_update.size(),
                 path_ids_to_remove.size(), paths_to_add.size(), paths_to_update.size());

    // 2) Removals first (paths then sinks)
    const auto t_rem_start = clock::now();
    LOG_CPP_INFO("[ConfigApplier] Removing: paths=%zu, sinks=%zu",
                 path_ids_to_remove.size(), sink_ids_to_remove.size());
    process_source_path_removals(path_ids_to_remove);
    process_sink_removals(sink_ids_to_remove);
    const auto t_rem_end = clock::now();
    LOG_CPP_INFO("[ConfigApplier] Removals: %lld ms", (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_rem_end - t_rem_start).count());

    // 3) Additions (paths then sinks)
    const auto t_add_start = clock::now();
    LOG_CPP_INFO("[ConfigApplier] Adding: paths=%zu, sinks=%zu", paths_to_add.size(), sinks_to_add.size());
    for (auto& path_param : paths_to_add) {
        const auto t_one_start = clock::now();
        const std::string filter_tag = get_filter_for_path_id(path_param.path_id, path_param.source_tag);
        const auto add_result = process_source_path_addition(path_param, filter_tag);
        if (add_result == SourcePathAddResult::Added) {
            InternalSourcePathState state;
            state.params = path_param;
            state.filter_tag = filter_tag;
            active_source_paths_[path_param.path_id] = std::move(state);
            const auto t_one_end = clock::now();
            LOG_CPP_INFO("[ConfigApplier] +Path id='%s' -> instance='%s' in %lld ms",
                         path_param.path_id.c_str(), path_param.generated_instance_id.c_str(),
                         (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_one_end - t_one_start).count());
        } else if (add_result == SourcePathAddResult::PendingStream) {
            const auto t_one_end = clock::now();
            LOG_CPP_INFO("[ConfigApplier] +Path id='%s' waiting for concrete stream '%s' (%lld ms)",
                         path_param.path_id.c_str(), filter_tag.c_str(),
                         (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_one_end - t_one_start).count());
        } else {
            const auto t_one_end = clock::now();
            LOG_CPP_ERROR("[ConfigApplier] +Path FAILED id='%s' after %lld ms",
                          path_param.path_id.c_str(),
                          (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_one_end - t_one_start).count());
        }
    }
    process_sink_additions(sinks_to_add);
    const auto t_add_end = clock::now();
    LOG_CPP_INFO("[ConfigApplier] Additions: %lld ms", (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_add_end - t_add_start).count());

    // 4) Updates
    const auto t_upd_start = clock::now();
    process_source_path_updates(paths_to_update);
    process_sink_updates(sinks_to_update);
    const auto t_upd_end = clock::now();
    LOG_CPP_INFO("[ConfigApplier] Updates: %lld ms", (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_upd_end - t_upd_start).count());

    const auto t_end = clock::now();
    LOG_CPP_INFO("[ConfigApplier] Finished apply_state in %lld ms",
                 (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count());
    return true;
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
    LOG_CPP_INFO("[ConfigApplier] Removing %zu sinks...", sink_ids_to_remove.size());
    for (const auto& sink_id : sink_ids_to_remove) {
        const auto t0 = std::chrono::steady_clock::now();
        LOG_CPP_DEBUG("[ConfigApplier]   - Removing sink: %s", sink_id.c_str());
        if (audio_manager_.remove_sink(sink_id)) {
            active_sinks_.erase(sink_id);
            const auto t1 = std::chrono::steady_clock::now();
            LOG_CPP_INFO("[ConfigApplier]     Sink %s removed (in %lld ms)", sink_id.c_str(),
                         (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
        } else {
            const auto t1 = std::chrono::steady_clock::now();
            LOG_CPP_ERROR("[ConfigApplier]     FAILED to remove sink: %s (after %lld ms). Internal state may be inconsistent.",
                          sink_id.c_str(),
                          (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
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
    LOG_CPP_INFO("[ConfigApplier] Adding %zu sinks...", sinks_to_add.size());
    for (const auto& sink_param : sinks_to_add) {
        const auto t0 = std::chrono::steady_clock::now();
        LOG_CPP_INFO("[ConfigApplier]   - Adding sink: id='%s' proto='%s' ip='%s' port=%d ch=%d rate=%d bit=%d",
                     sink_param.sink_id.c_str(),
                     sink_param.sink_engine_config.protocol.c_str(),
                     sink_param.sink_engine_config.output_ip.c_str(),
                     sink_param.sink_engine_config.output_port,
                     sink_param.sink_engine_config.channels,
                     sink_param.sink_engine_config.samplerate,
                     sink_param.sink_engine_config.bitdepth);
        if (audio_manager_.add_sink(sink_param.sink_engine_config)) {
            // Add to internal state.
            InternalSinkState new_internal_state;
            new_internal_state.params = sink_param; // Store desired params.
            // Crucially, clear connections initially; reconcile_connections will set them.
            new_internal_state.params.connected_source_path_ids.clear();
            active_sinks_[sink_param.sink_id] = new_internal_state;
            const auto t1 = std::chrono::steady_clock::now();
            LOG_CPP_INFO("[ConfigApplier]     Sink %s added in %lld ms", sink_param.sink_id.c_str(),
                         (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

            // Now reconcile connections for the newly added sink.
            LOG_CPP_DEBUG("[ConfigApplier]     -> reconcile_connections_for_sink(added %s)", sink_param.sink_id.c_str());
            reconcile_connections_for_sink(sink_param); // Pass desired state.
        } else {
            const auto t1 = std::chrono::steady_clock::now();
            LOG_CPP_ERROR("[ConfigApplier]     FAILED to add sink: %s (attempt took %lld ms)",
                          sink_param.sink_id.c_str(),
                          (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
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
    LOG_CPP_INFO("[ConfigApplier] Updating %zu sinks...", sinks_to_update.size());
    for (const auto& desired_sink_param : sinks_to_update) {
        const std::string& sink_id = desired_sink_param.sink_id;
        LOG_CPP_DEBUG("[ConfigApplier]   - Updating sink: %s", sink_id.c_str());

        auto active_it = active_sinks_.find(sink_id);
        if (active_it == active_sinks_.end()) {
            LOG_CPP_ERROR("[ConfigApplier]     Cannot update sink %s: Not found in active state (should not happen).", sink_id.c_str());
            continue;
        }
        InternalSinkState& current_internal_state = active_it->second;

        // Check if core engine parameters changed, requiring a re-creation of the sink.
        bool config_changed = !compare_sink_configs(current_internal_state.params.sink_engine_config, desired_sink_param.sink_engine_config);

        if (config_changed) {
            LOG_CPP_DEBUG("[ConfigApplier]     Core sink parameters changed for %s. Re-adding sink.", sink_id.c_str());
            // Remove the old sink from AudioManager.
            const auto t_rm0 = std::chrono::steady_clock::now();
            if (!audio_manager_.remove_sink(sink_id)) {
                 LOG_CPP_ERROR("[ConfigApplier]     Failed to remove sink %s during update. Aborting update for this sink.", sink_id.c_str());
                 continue; // Skip to next sink if removal failed.
            }
            const auto t_rm1 = std::chrono::steady_clock::now();
            LOG_CPP_INFO("[ConfigApplier]     Removed old sink %s in %lld ms", sink_id.c_str(),
                         (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_rm1 - t_rm0).count());
            // Add the sink back with the new config.
            const auto t_add0 = std::chrono::steady_clock::now();
            if (!audio_manager_.add_sink(desired_sink_param.sink_engine_config)) {
                 LOG_CPP_ERROR("[ConfigApplier]     Failed to re-add sink %s with new config during update. Sink is now removed.", sink_id.c_str());
                 active_sinks_.erase(active_it); // Remove from internal state as it couldn't be re-added.
                 continue; // Skip to next sink.
            }
            const auto t_add1 = std::chrono::steady_clock::now();
            LOG_CPP_INFO("[ConfigApplier]     Sink %s re-added with new config in %lld ms", sink_id.c_str(),
                         (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_add1 - t_add0).count());
            // Update internal config state.
            current_internal_state.params.sink_engine_config = desired_sink_param.sink_engine_config;
            // Clear internal connection state as they will be re-established.
            current_internal_state.params.connected_source_path_ids.clear();
        }

        // Always reconcile connections for updated sinks (whether config changed or only connections changed).
        LOG_CPP_DEBUG("[ConfigApplier]     -> reconcile_connections_for_sink(updated %s)", sink_id.c_str());
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

    return a.target_sink_id == b.target_sink_id &&
           volume_equal &&
           a.eq_values == b.eq_values &&
           a.delay_ms == b.delay_ms &&
           timeshift_equal &&
           a.target_output_channels == b.target_output_channels &&
           a.target_output_samplerate == b.target_output_samplerate &&
           a.source_input_channels == b.source_input_channels &&
           a.source_input_samplerate == b.source_input_samplerate &&
           a.source_input_bitdepth == b.source_input_bitdepth &&
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
            const std::string desired_filter = get_filter_for_path_id(desired_path.path_id, desired_path.source_tag);
            const bool filter_changed = current_state.filter_tag != desired_filter;
            AppliedSourcePathParams effective_desired = desired_path;
            const bool desired_is_wildcard = !effective_desired.source_tag.empty() && effective_desired.source_tag.back() == '*';
            if (desired_is_wildcard) {
                if (auto resolved = resolve_source_tag(desired_filter)) {
                    effective_desired.source_tag = *resolved;
                }
            }
            const bool params_changed = !compare_applied_source_path_params(current_state.params, effective_desired);
            if (filter_changed || params_changed) {
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
            const std::string source_tag = it->second.params.source_tag;
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
            if (!source_tag.empty() && (source_tag.rfind("ac:", 0) == 0 || source_tag.rfind("sr_out:", 0) == 0 || source_tag.rfind("hw:", 0) == 0)) {
                audio_manager_.remove_system_capture_reference(source_tag);
                LOG_CPP_DEBUG("    Released system capture reference for %s", source_tag.c_str());
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
AudioEngineConfigApplier::SourcePathAddResult AudioEngineConfigApplier::process_source_path_addition(
    AppliedSourcePathParams& path_param_to_add,
    const std::string& filter_tag) {
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    LOG_CPP_INFO("[ConfigApplier] +Path resolving filter='%s' path_id='%s'",
                 filter_tag.c_str(), path_param_to_add.path_id.c_str());
    bool has_concrete_tag = !path_param_to_add.source_tag.empty() && path_param_to_add.source_tag.back() != '*';
    if (!has_concrete_tag) {
        auto resolved_tag = resolve_source_tag(filter_tag);
        if (!resolved_tag) {
            LOG_CPP_INFO("[ConfigApplier] +Path id='%s': no concrete stream for filter '%s'; deferring",
                         path_param_to_add.path_id.c_str(), filter_tag.c_str());
            return SourcePathAddResult::PendingStream;
        }

        LOG_CPP_INFO("[ConfigApplier] +Path id='%s': filter '%s' resolved to '%s'",
                     path_param_to_add.path_id.c_str(), filter_tag.c_str(), resolved_tag->c_str());
        path_param_to_add.source_tag = *resolved_tag;
    } else {
        LOG_CPP_INFO("[ConfigApplier] +Path id='%s': using concrete stream '%s' from filter '%s'",
                     path_param_to_add.path_id.c_str(), path_param_to_add.source_tag.c_str(), filter_tag.c_str());
    }

    LOG_CPP_INFO("[ConfigApplier] +Path begin id='%s' filter='%s' resolved='%s' -> sink='%s' out=%dch@%dHz in=%dch@%dHz/%dbit",
                 path_param_to_add.path_id.c_str(),
                 filter_tag.c_str(),
                 path_param_to_add.source_tag.c_str(),
                 path_param_to_add.target_sink_id.c_str(),
                 path_param_to_add.target_output_channels,
                 path_param_to_add.target_output_samplerate,
                 path_param_to_add.source_input_channels,
                 path_param_to_add.source_input_samplerate,
                 path_param_to_add.source_input_bitdepth);
    
    // 1. Create C++ SourceConfig from the provided parameters.
    audio::SourceConfig cpp_source_config;
    std::string source_tag = path_param_to_add.source_tag;
    const bool is_alsa_capture_tag = !source_tag.empty() && source_tag.rfind("ac:", 0) == 0;
    const bool is_fifo_capture_tag = !source_tag.empty() && source_tag.rfind("sr_out:", 0) == 0;
    const bool is_hw_capture_tag = !source_tag.empty() && source_tag.rfind("hw:", 0) == 0;

    if (is_fifo_capture_tag) {
        std::string label = source_tag.substr(7);
        std::string sanitized = sanitize_screamrouter_label(label);
        if (!sanitized.empty()) {
            source_tag = "sr_out:" + sanitized;
            path_param_to_add.source_tag = source_tag;
        }
    }

    cpp_source_config.tag = source_tag;
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
    cpp_source_config.initial_timeshift_sec = path_param_to_add.timeshift_sec;
    cpp_source_config.target_output_channels = path_param_to_add.target_output_channels;
    cpp_source_config.target_output_samplerate = path_param_to_add.target_output_samplerate;

    bool added_capture_reference = false;

    if (!source_tag.empty() && (is_alsa_capture_tag || is_fifo_capture_tag || is_hw_capture_tag)) {
        audio::CaptureParams capture_params;
        if (path_param_to_add.source_input_channels > 0) {
            capture_params.channels = static_cast<unsigned int>(path_param_to_add.source_input_channels);
        } else if (path_param_to_add.target_output_channels > 0) {
            capture_params.channels = static_cast<unsigned int>(path_param_to_add.target_output_channels);
        }
        if (path_param_to_add.source_input_samplerate > 0) {
            capture_params.sample_rate = static_cast<unsigned int>(path_param_to_add.source_input_samplerate);
        } else if (path_param_to_add.target_output_samplerate > 0) {
            capture_params.sample_rate = static_cast<unsigned int>(path_param_to_add.target_output_samplerate);
        }
        if (path_param_to_add.source_input_bitdepth > 0) {
            capture_params.bit_depth = static_cast<unsigned int>(path_param_to_add.source_input_bitdepth);
        }

        if (is_alsa_capture_tag || is_fifo_capture_tag) {
            try {
                const auto registry = audio_manager_.list_system_devices();
                auto info_it = registry.find(path_param_to_add.source_tag);
                if (info_it != registry.end()) {
                    const auto& info = info_it->second;
                    if (!info.hw_id.empty()) {
                        capture_params.hw_id = info.hw_id;
                    }
                    if (info.channels.min > 0) {
                        capture_params.channels = info.channels.min;
                    }
                    if (info.sample_rates.min > 0) {
                        capture_params.sample_rate = info.sample_rates.min;
                    }
                    if (info.bit_depth > 0) {
                        capture_params.bit_depth = info.bit_depth;
                    }
                }
            } catch (const std::exception& ex) {
                LOG_CPP_WARNING("    Failed to resolve device info for %s: %s", path_param_to_add.source_tag.c_str(), ex.what());
            }
        }

        const auto t_cap0 = clock::now();
        if (audio_manager_.add_system_capture_reference(path_param_to_add.source_tag, capture_params)) {
            added_capture_reference = true;
            const auto t_cap1 = clock::now();
            LOG_CPP_INFO("[ConfigApplier]     Capture ready for %s in %lld ms",
                         path_param_to_add.source_tag.c_str(),
                         (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_cap1 - t_cap0).count());
        } else {
            const auto t_cap1 = clock::now();
            LOG_CPP_WARNING("[ConfigApplier]     Failed to init capture for %s (attempt %lld ms)",
                            path_param_to_add.source_tag.c_str(),
                            (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_cap1 - t_cap0).count());
        }
    }

    // 2. Call AudioManager to configure the source and get an instance ID.
    const auto t_cfg0 = clock::now();
    std::string instance_id = audio_manager_.configure_source(cpp_source_config);
    const auto t_cfg1 = clock::now();

    // 3. Handle the result and update the parameter struct.
    if (instance_id.empty()) {
        LOG_CPP_ERROR("[ConfigApplier]     FAILED to configure source for path_id: %s source_tag: %s (took %lld ms)",
                      path_param_to_add.path_id.c_str(), path_param_to_add.source_tag.c_str(),
                      (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_cfg1 - t_cfg0).count());
        path_param_to_add.generated_instance_id.clear(); // Ensure ID is empty on failure.
        if (added_capture_reference) {
            audio_manager_.remove_system_capture_reference(path_param_to_add.source_tag);
        }
        return SourcePathAddResult::Failed;
    } else {
        LOG_CPP_INFO("[ConfigApplier]     Configured source for path_id: %s, instance_id: %s (in %lld ms)",
                     path_param_to_add.path_id.c_str(), instance_id.c_str(),
                     (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_cfg1 - t_cfg0).count());
        path_param_to_add.generated_instance_id = instance_id; // Store the generated ID.
        
        // Apply the initial speaker layouts map for the newly added source.
        const auto t_up0 = clock::now();
        LOG_CPP_DEBUG("[ConfigApplier]     Applying initial speaker_layouts_map for new source instance %s", instance_id.c_str());
        
        audio::SourceParameterUpdates updates;
        updates.speaker_layouts_map = path_param_to_add.speaker_layouts_map;
        audio_manager_.update_source_parameters(instance_id, updates);
        const auto t_up1 = clock::now();
        LOG_CPP_DEBUG("[ConfigApplier]     Initial speaker_layouts_map applied for %s in %lld ms",
                      instance_id.c_str(),
                      (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_up1 - t_up0).count());

        const auto t1 = clock::now();
        LOG_CPP_INFO("[ConfigApplier] +Path complete id='%s' total %lld ms",
                     path_param_to_add.path_id.c_str(),
                     (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

        // The caller (apply_state) is responsible for adding this to the active_source_paths_ map.
        return SourcePathAddResult::Added;
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
    LOG_CPP_INFO("[ConfigApplier] Updating %zu source path(s)...", paths_to_update.size());
    for (const auto& desired_path_param : paths_to_update) {
        const std::string& path_id = desired_path_param.path_id;
        LOG_CPP_DEBUG("[ConfigApplier]   - Updating path: %s", path_id.c_str());

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

        const std::string filter_tag = get_filter_for_path_id(path_id, desired_path_param.source_tag);
        LOG_CPP_INFO("[ConfigApplier]   -> Updating path %s using filter '%s'",
                     path_id.c_str(), filter_tag.c_str());

        AppliedSourcePathParams desired_params = desired_path_param;
        const bool desired_is_wildcard = !desired_params.source_tag.empty() && desired_params.source_tag.back() == '*';
        if (desired_is_wildcard) {
            auto resolved_tag = resolve_source_tag(filter_tag);
            if (!resolved_tag) {
                LOG_CPP_WARNING("    Path %s: filter '%s' unresolved; remaining wildcard-bound.",
                                path_id.c_str(), filter_tag.c_str());
                desired_params.source_tag = filter_tag;
            } else {
                LOG_CPP_INFO("    Path %s: filter '%s' resolved to '%s'",
                             path_id.c_str(), filter_tag.c_str(), resolved_tag->c_str());
                desired_params.source_tag = *resolved_tag;
            }
        }

        // Check for fundamental changes requiring re-creation of the source processor.
        bool fundamental_change =
            current_path_state.params.source_tag != desired_params.source_tag ||
            current_path_state.params.target_output_channels != desired_params.target_output_channels ||
            current_path_state.params.target_output_samplerate != desired_params.target_output_samplerate ||
            current_path_state.params.source_input_channels != desired_params.source_input_channels ||
            current_path_state.params.source_input_samplerate != desired_params.source_input_samplerate ||
            current_path_state.params.source_input_bitdepth != desired_params.source_input_bitdepth;

        if (fundamental_change) {
            const auto t_recreate0 = std::chrono::steady_clock::now();
            LOG_CPP_DEBUG("[ConfigApplier]     Fundamental change detected for %s. Re-creating instance.", path_id.c_str());
            // Remove the old instance.
            if (!audio_manager_.remove_source(instance_id)) {
                 LOG_CPP_ERROR("[ConfigApplier]     Failed to remove old instance %s. Aborting this path.", instance_id.c_str());
                 continue; // Skip to the next path.
            }
            // Remove from internal state immediately.
            active_source_paths_.erase(active_it);

            // Add a new instance.
            AppliedSourcePathParams temp_param_for_add = desired_path_param;
            auto recreate_result = process_source_path_addition(temp_param_for_add, filter_tag);
            if (recreate_result == SourcePathAddResult::Added) {
                // Add the new state back to the internal map.
                InternalSourcePathState state;
                state.params = temp_param_for_add;
                state.filter_tag = filter_tag;
                active_source_paths_[temp_param_for_add.path_id] = std::move(state);
                LOG_CPP_DEBUG("[ConfigApplier]     Re-created %s with new instance_id: %s",
                             path_id.c_str(), temp_param_for_add.generated_instance_id.c_str());
                // Connections for this new instance_id will be re-established by the sink update logic.
            } else if (recreate_result == SourcePathAddResult::PendingStream) {
                LOG_CPP_INFO("[ConfigApplier]     Re-create of %s pending stream match for filter '%s'",
                             path_id.c_str(), filter_tag.c_str());
            } else {
                LOG_CPP_ERROR("[ConfigApplier]     Failed to re-create %s after fundamental change. Path is removed.", path_id.c_str());
                // Path remains removed from active_source_paths_.
            }
            const auto t_recreate1 = std::chrono::steady_clock::now();
            LOG_CPP_INFO("[ConfigApplier]     Re-create cycle for %s took %lld ms",
                         path_id.c_str(),
                         (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_recreate1 - t_recreate0).count());
            // Whether re-add succeeded or failed, we are done with this path for this update cycle.
            continue;
        }

        // Process non-fundamental parameter updates.
        const auto t_up0 = std::chrono::steady_clock::now();
        LOG_CPP_DEBUG("[ConfigApplier]     Applying parameter updates for %s (Instance: %s)", path_id.c_str(), instance_id.c_str());

        audio::SourceParameterUpdates updates;
        updates.volume = desired_params.volume;
        if (desired_params.eq_values.size() == EQ_BANDS) {
            updates.eq_values = desired_params.eq_values;
            updates.eq_normalization = desired_params.eq_normalization;
        } else {
            LOG_CPP_ERROR("    Invalid EQ size (%zu) for path update %s. Skipping EQ update.",
                          desired_params.eq_values.size(), path_id.c_str());
        }
        updates.volume_normalization = desired_params.volume_normalization;
        updates.delay_ms = desired_params.delay_ms;
        updates.timeshift_sec = desired_params.timeshift_sec;
        updates.speaker_layouts_map = desired_params.speaker_layouts_map;

        audio_manager_.update_source_parameters(instance_id, updates);
        const auto t_up1 = std::chrono::steady_clock::now();
        LOG_CPP_INFO("[ConfigApplier]     Param update for %s took %lld ms",
                     path_id.c_str(),
                     (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_up1 - t_up0).count());

        // Update the internal state to reflect the desired parameters.
        // The generated instance ID must be preserved.
        std::string preserved_instance_id = current_path_state.params.generated_instance_id;
        current_path_state.params = desired_params;
        current_path_state.params.generated_instance_id = preserved_instance_id;
        current_path_state.filter_tag = filter_tag;
        LOG_CPP_DEBUG("[ConfigApplier]     Internal state updated for path %s", path_id.c_str());
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
    LOG_CPP_DEBUG("[ConfigApplier] Reconciling connections for sink: %s", sink_id.c_str());

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
    std::set<std::string> updated_path_ids_set = current_path_ids_set; // Track the connections that are actually established.

    const std::vector<std::string>& desired_path_ids_vec = desired_sink_params.connected_source_path_ids;
    std::set<std::string> desired_path_ids_set(desired_path_ids_vec.begin(), desired_path_ids_vec.end());

    // Log current vs. desired connections for debugging.
    LOG_CPP_DEBUG("[ConfigApplier]     Current connection path IDs (%zu):", current_path_ids_set.size());
    if (current_path_ids_set.empty()) { LOG_CPP_DEBUG("      (None)"); }
    for(const auto& id : current_path_ids_set) { LOG_CPP_DEBUG("      - %s", id.c_str()); }
    LOG_CPP_DEBUG("[ConfigApplier]     Desired connection path IDs (%zu):", desired_path_ids_set.size());
    if (desired_path_ids_set.empty()) { LOG_CPP_DEBUG("      (None)"); }
    for(const auto& id : desired_path_ids_set) { LOG_CPP_DEBUG("      - %s", id.c_str()); }

    // 3. Identify and process connections to add.
    LOG_CPP_DEBUG("[ConfigApplier]     Checking connections to add...");
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

            LOG_CPP_DEBUG("[ConfigApplier]       + Connecting Source:");
            LOG_CPP_DEBUG("          Path ID: %s", desired_path_id.c_str());
            LOG_CPP_DEBUG("          Instance ID: %s", source_instance_id.c_str());
            LOG_CPP_DEBUG("          Source Tag: %s", source_params.source_tag.c_str());
            LOG_CPP_DEBUG("        To Sink:");
            LOG_CPP_DEBUG("          Sink ID: %s", sink_id.c_str());
            LOG_CPP_DEBUG("          Target: %s:%d", sink_config.output_ip.c_str(), sink_config.output_port);
            LOG_CPP_DEBUG("          Format: %dch@%dHz, %dbit", sink_config.channels, sink_config.samplerate, sink_config.bitdepth);

            // Call AudioManager to establish the connection.
            const auto t_c0 = std::chrono::steady_clock::now();
            if (!audio_manager_.connect_source_sink(source_instance_id, sink_id)) {
                 const auto t_c1 = std::chrono::steady_clock::now();
                 LOG_CPP_ERROR("[ConfigApplier]         -> connect_source_sink FAILED (%lld ms)",
                               (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_c1 - t_c0).count());
                 LOG_CPP_WARNING("[ConfigApplier]         -> Connection attempt for path %s will be retried on the next apply_state cycle.",
                                  desired_path_id.c_str());
                 updated_path_ids_set.erase(desired_path_id); // Ensure the shadow state reflects the failure.
            } else {
                 const auto t_c1 = std::chrono::steady_clock::now();
                 LOG_CPP_INFO("[ConfigApplier]         -> Connection successful in %lld ms",
                              (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_c1 - t_c0).count());
                 updated_path_ids_set.insert(desired_path_id);
            }
        }
    }

    // 4. Identify and process connections to remove.
    LOG_CPP_DEBUG("[ConfigApplier]     Checking connections to remove...");
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
            const auto t_d0 = std::chrono::steady_clock::now();
            if (!audio_manager_.disconnect_source_sink(source_instance_id, sink_id)) {
                 const auto t_d1 = std::chrono::steady_clock::now();
                 LOG_CPP_ERROR("[ConfigApplier]         -> disconnect_source_sink FAILED (%lld ms) (might be expected)",
                               (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_d1 - t_d0).count());
            } else {
                 const auto t_d1 = std::chrono::steady_clock::now();
                 LOG_CPP_INFO("[ConfigApplier]         -> Disconnected in %lld ms",
                              (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t_d1 - t_d0).count());
            }
            updated_path_ids_set.erase(current_path_id);
        }
    }

    // 5. Update the internal state to match the desired state.
    std::vector<std::string> resulting_connections;
    resulting_connections.reserve(updated_path_ids_set.size());
    for (const auto& desired_path_id : desired_path_ids_vec) {
        if (updated_path_ids_set.find(desired_path_id) != updated_path_ids_set.end()) {
            resulting_connections.push_back(desired_path_id);
        }
    }

    current_sink_state.params.connected_source_path_ids = std::move(resulting_connections);
    LOG_CPP_DEBUG("[ConfigApplier]     Internal connection state updated for sink %s", sink_id.c_str());
}


std::optional<std::string> AudioEngineConfigApplier::resolve_source_tag(const std::string& requested_tag) {
    LOG_CPP_DEBUG("[ConfigApplier] resolve_source_tag('%s')", requested_tag.c_str());
    if (requested_tag.empty()) {
        LOG_CPP_DEBUG("[ConfigApplier] resolve_source_tag('%s') => <empty>", requested_tag.c_str());
        return std::nullopt;
    }
    if (requested_tag.back() != '*') {
        LOG_CPP_DEBUG("[ConfigApplier] resolve_source_tag('%s') -> concrete (no wildcard)", requested_tag.c_str());
        return requested_tag;
    }

    const std::string prefix = requested_tag.substr(0, requested_tag.size() - 1);
    auto resolved = audio_manager_.resolve_stream_tag(requested_tag);
    if (resolved && resolved->rfind(prefix, 0) == 0) {
        LOG_CPP_INFO("[ConfigApplier] resolve_source_tag('%s') => '%s'", requested_tag.c_str(), resolved->c_str());
        return resolved;
    }
    LOG_CPP_DEBUG("[ConfigApplier] resolve_source_tag('%s') => <none>", requested_tag.c_str());
    return std::nullopt;
}

DesiredEngineState AudioEngineConfigApplier::build_effective_state(const DesiredEngineState& base_state) {
    DesiredEngineState effective_state;
    effective_state.sinks = base_state.sinks;
    effective_state.source_paths.reserve(base_state.source_paths.size());

    std::unordered_map<std::string, std::vector<std::string>> clone_ids_by_template;
    clone_filter_lookup_.clear();

    for (const auto& path : base_state.source_paths) {
        effective_state.source_paths.push_back(path);
        clone_filter_lookup_[path.path_id] = path.source_tag;
        if (path.source_tag.empty() || path.source_tag.back() != '*') {
            continue;
        }

        auto active_streams = audio_manager_.list_stream_tags_for_wildcard(path.source_tag);
        if (active_streams.empty()) {
            continue;
        }
        auto& clone_ids = clone_ids_by_template[path.path_id];
        clone_ids.reserve(active_streams.size());
        for (const auto& concrete_tag : active_streams) {
            AppliedSourcePathParams clone = path;
            clone.path_id = path.path_id + "::" + sanitize_clone_suffix(concrete_tag);
            clone.source_tag = concrete_tag;
            effective_state.source_paths.push_back(clone);
            clone_ids.push_back(clone.path_id);
            clone_filter_lookup_[clone.path_id] = path.source_tag;
        }
    }

    if (!clone_ids_by_template.empty()) {
        for (auto& sink : effective_state.sinks) {
            std::vector<std::string> updated_connections;
            for (const auto& connection_id : sink.connected_source_path_ids) {
                auto clone_it = clone_ids_by_template.find(connection_id);
                if (clone_it != clone_ids_by_template.end() && !clone_it->second.empty()) {
                    updated_connections.insert(updated_connections.end(),
                                               clone_it->second.begin(),
                                               clone_it->second.end());
                } else {
                    updated_connections.push_back(connection_id);
                }
            }
            sink.connected_source_path_ids = std::move(updated_connections);
        }
    }

    return effective_state;
}

std::string AudioEngineConfigApplier::get_filter_for_path_id(const std::string& path_id, const std::string& fallback) const {
    auto it = clone_filter_lookup_.find(path_id);
    if (it != clone_filter_lookup_.end()) {
        return it->second;
    }
    return fallback;
}

void AudioEngineConfigApplier::handle_stream_tag_resolved(const std::string& wildcard_tag,
                                                          const std::string& concrete_tag) {
    {
        std::lock_guard<std::recursive_mutex> lock(apply_mutex_);
        if (!cached_desired_state_valid_) {
            LOG_CPP_DEBUG("[ConfigApplier] Ignoring stream resolution '%s' -> '%s' (no cached state)",
                          wildcard_tag.c_str(), concrete_tag.c_str());
            return;
        }
    }

    LOG_CPP_INFO("[ConfigApplier] Pulse wildcard '%s' resolved to '%s'; reapplying desired state",
                 wildcard_tag.c_str(), concrete_tag.c_str());
    reapply_cached_state("pulse_stream_resolved");
}

void AudioEngineConfigApplier::handle_stream_tag_removed(const std::string& wildcard_tag) {
    {
        std::lock_guard<std::recursive_mutex> lock(apply_mutex_);
        if (!cached_desired_state_valid_) {
            LOG_CPP_DEBUG("[ConfigApplier] Ignoring stream removal '%s' (no cached state)",
                          wildcard_tag.c_str());
            return;
        }
    }

    LOG_CPP_INFO("[ConfigApplier] Pulse wildcard '%s' removed; reapplying desired state",
                 wildcard_tag.c_str());
    reapply_cached_state("pulse_stream_removed");
}

void AudioEngineConfigApplier::reapply_cached_state(const char* reason) {
    DesiredEngineState snapshot;
    {
        std::lock_guard<std::recursive_mutex> lock(apply_mutex_);
        if (!cached_desired_state_valid_) {
            LOG_CPP_WARNING("[ConfigApplier] Cannot reapply (%s): no cached desired state",
                            reason ? reason : "unspecified");
            return;
        }
        snapshot = cached_desired_state_;
    }

    LOG_CPP_INFO("[ConfigApplier] Reapplying cached desired state (reason=%s)",
                 reason ? reason : "unspecified");
    apply_state(std::move(snapshot));
}


} // namespace config
} // namespace screamrouter
