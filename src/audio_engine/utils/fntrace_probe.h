#pragma once

// A tiny function compiled with instrumentation enabled, so calling it
// guarantees at least one B/E trace event if tracing is active.
#if defined(__GNUC__) || defined(__clang__)
#  define SR_NOINLINE __attribute__((noinline))
#else
#  define SR_NOINLINE
#endif

SR_NOINLINE void sr_fntrace_probe();
