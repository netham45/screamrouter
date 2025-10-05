/**
 * @file audio_engine_config_applier.h
 * @brief Defines the AudioEngineConfigApplier class for managing audio engine state.
 * @details This class is responsible for taking a desired configuration state and applying it
 *          to the AudioManager. It handles the logic for adding, removing, and updating
 *          sinks and source paths to match the desired state, acting as a bridge between
 *          the configuration system and the live audio engine.
 */
#ifndef AUDIO_ENGINE_CONFIG_APPLIER_H
#define AUDIO_ENGINE_CONFIG_APPLIER_H

#include <string>
#include <vector>
#include <map>
#include <set>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

#include "audio_engine_config_types.h"

// Forward declare AudioManager to reduce compile-time dependencies.
namespace screamrouter { namespace audio { class AudioManager; } }

namespace screamrouter {
namespace config {

/**
 * @class AudioEngineConfigApplier
 * @brief Manages and applies configuration changes to the audio engine.
 * @details This class holds a "shadow" state of the audio engine's configuration
 *          and provides a method to reconcile this state with a new desired state.
 *          It interacts directly with the AudioManager to enact the necessary changes.
 */
class AudioEngineConfigApplier {
public:
    /**
     * @brief Constructs an AudioEngineConfigApplier.
     * @param audio_manager A reference to the AudioManager instance to be controlled.
     */
    explicit AudioEngineConfigApplier(audio::AudioManager& audio_manager);
    
    /**
     * @brief Destructor.
     */
    ~AudioEngineConfigApplier();

    // Delete copy and move operations to ensure a single instance manages the state.
    AudioEngineConfigApplier(const AudioEngineConfigApplier&) = delete;
    AudioEngineConfigApplier& operator=(const AudioEngineConfigApplier&) = delete;
    AudioEngineConfigApplier(AudioEngineConfigApplier&&) = delete;
    AudioEngineConfigApplier& operator=(AudioEngineConfigApplier&&) = delete;

    /**
     * @brief Applies the desired configuration state to the AudioManager.
     * @param desired_state The target state including sinks, source paths, and connections.
     * @return true if the state was applied successfully, false otherwise.
     */
    bool apply_state(DesiredEngineState desired_state);

private:
    /** @brief Reference to the AudioManager instance being controlled. */
    audio::AudioManager& audio_manager_;

    // --- Internal State Representation (Shadow State) ---
    
    /**
     * @struct InternalSourcePathState
     * @brief Holds the internal state of an active source path.
     */
    struct InternalSourcePathState {
        AppliedSourcePathParams params;
    };
    /** @brief Map storing active source paths, keyed by path_id. */
    std::map<std::string, InternalSourcePathState> active_source_paths_;

    /**
     * @struct InternalSinkState
     * @brief Holds the internal state of an active sink.
     */
    struct InternalSinkState {
        AppliedSinkParams params;
    };
    /** @brief Map storing active sinks, keyed by sink_id. */
    std::map<std::string, InternalSinkState> active_sinks_;

    // --- Private Helper Methods ---

    /**
     * @brief Reconciles the desired sinks with the active sinks.
     * @param desired_sinks The vector of desired sink configurations.
     * @param sink_ids_to_remove Output vector of sink IDs to be removed.
     * @param sinks_to_add Output vector of sink configurations to be added.
     * @param sinks_to_update Output vector of sink configurations to be updated.
     */
    void reconcile_sinks(
        const std::vector<AppliedSinkParams>& desired_sinks,
        std::vector<std::string>& sink_ids_to_remove,
        std::vector<AppliedSinkParams>& sinks_to_add,
        std::vector<AppliedSinkParams>& sinks_to_update);

    /**
     * @brief Reconciles the desired source paths with the active source paths.
     * @param desired_source_paths The vector of desired source path configurations.
     * @param path_ids_to_remove Output vector of path IDs to be removed.
     * @param paths_to_add Output vector of path configurations to be added.
     * @param paths_to_update Output vector of path configurations to be updated.
     */
    void reconcile_source_paths(
        const std::vector<AppliedSourcePathParams>& desired_source_paths,
        std::vector<std::string>& path_ids_to_remove,
        std::vector<AppliedSourcePathParams>& paths_to_add,
        std::vector<AppliedSourcePathParams>& paths_to_update);

    void process_sink_removals(const std::vector<std::string>& sink_ids_to_remove);
    void process_sink_additions(const std::vector<AppliedSinkParams>& sinks_to_add);
    void process_sink_updates(const std::vector<AppliedSinkParams>& sinks_to_update);
    
    void process_source_path_removals(const std::vector<std::string>& path_ids_to_remove);
    bool process_source_path_addition(AppliedSourcePathParams& path_to_add);
    void process_source_path_updates(const std::vector<AppliedSourcePathParams>& paths_to_update);

    /**
     * @brief Reconciles the connections for a specific sink.
     * @param desired_sink_params The desired parameters for the sink, including its connections.
     */
    void reconcile_connections_for_sink(const AppliedSinkParams& desired_sink_params);
};
    
/**
 * @brief Binds the AudioEngineConfigApplier class to a Python module.
 * @param m The pybind11 module to which the class will be bound.
 */
inline void bind_config_applier(pybind11::module_ &m) {
    namespace py = pybind11;
    py::class_<AudioEngineConfigApplier>(m, "AudioEngineConfigApplier", "Applies desired configuration state to the C++ AudioManager")
        .def(py::init<audio::AudioManager&>(),
             py::arg("audio_manager"),
             "Constructor, takes an AudioManager instance")
        
       .def("apply_state", &AudioEngineConfigApplier::apply_state, py::arg("desired_state"), py::call_guard<py::gil_scoped_release>(), "Applies a desired state.");
}

} // namespace config
} // namespace screamrouter

#endif // AUDIO_ENGINE_CONFIG_APPLIER_H
