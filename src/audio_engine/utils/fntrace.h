// Lightweight function-level tracing hooks for Chrome Trace format.
// Build-time: enable -finstrument-functions via CMake when env var set.
// Runtime: writing is enabled only if SCREAMROUTER_TRACE is set.

#pragma once

#include <cstddef>

#if defined(__GNUC__) || defined(__clang__)
#  define SR_NO_INSTRUMENT __attribute__((no_instrument_function))
#else
#  define SR_NO_INSTRUMENT
#endif

namespace srtrace {

SR_NO_INSTRUMENT void init_if_needed();
SR_NO_INSTRUMENT bool is_enabled();
SR_NO_INSTRUMENT void log_event_begin(const void* fn, const void* call_site);
SR_NO_INSTRUMENT void log_event_end(const void* fn, const void* call_site);
SR_NO_INSTRUMENT void shutdown();

} // namespace srtrace

extern "C" {

// GCC/Clang instrumentation hooks. These must not themselves be instrumented.
SR_NO_INSTRUMENT void __cyg_profile_func_enter(void* this_fn, void* call_site);
SR_NO_INSTRUMENT void __cyg_profile_func_exit(void* this_fn, void* call_site);

}

