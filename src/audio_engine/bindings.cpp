#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

// Include the headers that now contain the binding functions.
// These headers include each other as needed, so we just need the top-level ones.
#include "managers/audio_manager.h"
#include "configuration/audio_engine_config_applier.h"
#include "utils/cpp_logger.h"

// Note: audio_types.h and audio_engine_config_types.h are included by the headers above.

namespace py = pybind11;
using namespace screamrouter;

// Define the Python module using the PYBIND11_MODULE macro
// The first argument is the name of the Python module as it will be imported (e.g., `import screamrouter_audio_engine`)
// The second argument `m` is the py::module_ object representing the module
PYBIND11_MODULE(screamrouter_audio_engine, m) {
    m.doc() = "ScreamRouter C++ Audio Engine Extension"; // Optional module docstring

    // --- Call Binding Functions in Dependency Order ---

    // 1. Logger has no dependencies on other bound types
    audio::logging::bind_logger(m);

    // 2. Audio types are fundamental and used by other bindings
    audio::bind_audio_types(m);

    // 3. Config types depend on audio types (e.g., SinkConfig, CppSpeakerLayout)
    config::bind_config_types(m);

    // 4. Audio manager depends on almost all of the above types
    audio::bind_audio_manager(m);

    // 5. Config applier depends on AudioManager and the config state types
    config::bind_config_applier(m);

    // --- Bind Global Constants ---
    m.attr("EQ_BANDS") = py::int_(audio::EQ_BANDS);
}
