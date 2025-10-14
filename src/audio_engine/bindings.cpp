/**
 * @file bindings.cpp
 * @brief Defines the Python module for the ScreamRouter C++ audio engine.
 * @details This file uses pybind11 to create the `screamrouter_audio_engine` Python module.
 *          It imports binding functions from various components (like AudioManager, configuration,
 *          and logger) and calls them in the correct dependency order to construct the module.
 *          It also binds global constants to make them accessible from Python.
 */
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

// Include the headers that now contain the binding functions.
// These headers include each other as needed, so we just need the top-level ones.
#include "managers/audio_manager.h"
#include "configuration/audio_engine_config_applier.h"
#include "configuration/audio_engine_config_types.h"
#include "utils/cpp_logger.h"
#include "audio_types.h"
#include "synchronization/global_synchronization_clock.h"
#include "synchronization/sink_synchronization_coordinator.h"

// Note: audio_types.h and audio_engine_config_types.h are included by the headers above.

namespace py = pybind11;
using namespace screamrouter;

/**
 * @brief The main entry point for the pybind11 module definition.
 * @param m A `py::module_` object representing the Python module being created.
 *
 * This macro defines the `screamrouter_audio_engine` module and orchestrates the
 * binding of all C++ classes, functions, and constants. The bindings are
 * called in a specific order to ensure that dependencies are met. For example,
 * basic data types are bound before the classes that use them.
 */
PYBIND11_MODULE(screamrouter_audio_engine, m) {
    m.doc() = "ScreamRouter C++ Audio Engine Extension"; // Optional module docstring

    // --- Call Binding Functions in Dependency Order ---

    // 1. Logger has no dependencies on other bound types
    audio::logging::bind_logger(m);

    // 2. Audio types are fundamental and used by other bindings
    audio::bind_audio_types(m);

    // 2.5. Bind synchronization statistics structures
    py::class_<audio::SyncStats>(m, "SyncStats")
        .def(py::init<>())
        .def_readonly("active_sinks", &audio::SyncStats::active_sinks)
        .def_readonly("current_playback_timestamp", &audio::SyncStats::current_playback_timestamp)
        .def_readonly("max_drift_ppm", &audio::SyncStats::max_drift_ppm)
        .def_readonly("avg_barrier_wait_ms", &audio::SyncStats::avg_barrier_wait_ms)
        .def_readonly("total_barrier_timeouts", &audio::SyncStats::total_barrier_timeouts);

    py::class_<audio::CoordinatorStats>(m, "CoordinatorStats")
        .def(py::init<>())
        .def_readonly("total_dispatches", &audio::CoordinatorStats::total_dispatches)
        .def_readonly("barrier_timeouts", &audio::CoordinatorStats::barrier_timeouts)
        .def_readonly("underruns", &audio::CoordinatorStats::underruns)
        .def_readonly("current_rate_adjustment", &audio::CoordinatorStats::current_rate_adjustment)
        .def_readonly("total_samples_output", &audio::CoordinatorStats::total_samples_output)
        .def_readonly("coordination_enabled", &audio::CoordinatorStats::coordination_enabled);

    // 3. Config types depend on audio types (e.g., SinkConfig, CppSpeakerLayout)
    config::bind_config_types(m);

    // 4. Audio manager depends on almost all of the above types
    audio::bind_audio_manager(m);

    // 5. Config applier depends on AudioManager and the config state types
    config::bind_config_applier(m);

    // --- Bind Global Constants ---
    m.attr("EQ_BANDS") = py::int_(audio::EQ_BANDS);
}
