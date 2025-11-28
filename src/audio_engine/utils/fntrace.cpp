// Function-level tracing to Chrome Trace Event JSON.
// Build-time enable using -finstrument-functions (CMake controls this).
// Runtime guard: set env var SCREAMROUTER_TRACE to enable writing.

#include "fntrace.h"

#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>

#if !defined(_MSC_VER)
#  include <sys/types.h>
#  include <unistd.h>
#endif

namespace {

struct TraceState {
    std::atomic<bool> inited{false};
    bool enabled{false};
    FILE* fp{nullptr};
    bool first_event{true};
    int pid{0};
    std::mutex mtx; // serialize writes
};

TraceState g_state;

#if defined(__GNUC__) || defined(__clang__)
#  define SR_NO_INSTRUMENT __attribute__((no_instrument_function))
#else
#  define SR_NO_INSTRUMENT
#endif

SR_NO_INSTRUMENT static uint64_t thread_id_u64() {
    auto tid = std::this_thread::get_id();
    return static_cast<uint64_t>(std::hash<std::thread::id>{}(tid));
}

SR_NO_INSTRUMENT static void write_header_unlocked() {
    if (!g_state.fp) return;
    std::fputs("{\n\"traceEvents\":[\n", g_state.fp);
    g_state.first_event = true;
    std::fflush(g_state.fp);
}

SR_NO_INSTRUMENT static void write_footer_unlocked() {
    if (!g_state.fp) return;
    std::fputs("\n]\n}\n", g_state.fp);
}

struct ShutdownGuard {
    SR_NO_INSTRUMENT ~ShutdownGuard() { srtrace::shutdown(); }
};

ShutdownGuard g_shutdown_guard;

} // namespace

namespace srtrace {

SR_NO_INSTRUMENT void init_if_needed() {
    bool expected = false;
    if (!g_state.inited.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    const char* enabled_env = std::getenv("SCREAMROUTER_TRACE");
    g_state.enabled = (enabled_env && *enabled_env);

    if (!g_state.enabled) {
        return;
    }

    const char* file_path = std::getenv("SCREAMROUTER_TRACE_FILE");
    if (!file_path || !*file_path) {
        file_path = "screamrouter-trace.json";
    }

    g_state.fp = std::fopen(file_path, "w");
    if (!g_state.fp) {
        g_state.enabled = false;
        return;
    }

#if defined(_MSC_VER)
    g_state.pid = 0;
#else
    g_state.pid = static_cast<int>(::getpid());
#endif

    static char buffer[1 << 20];
    std::setvbuf(g_state.fp, buffer, _IOFBF, sizeof(buffer));

    std::lock_guard<std::mutex> lock(g_state.mtx);
    write_header_unlocked();
    // Ensure footer is written even if the module/process exits without unloading.
    std::atexit(&srtrace::shutdown);

    // Write initial metadata so the file isn't empty and tools can label tracks.
    // Process name metadata
    if (g_state.fp) {
        const uint64_t tid = thread_id_u64();
        const char* proc_name = "screamrouter_audio_engine";
        if (!g_state.first_event) std::fputs(",\n", g_state.fp);
        g_state.first_event = false;
        std::fprintf(g_state.fp,
                     "{\"name\":\"process_name\",\"ph\":\"M\",\"pid\":%d,\"tid\":0,\"args\":{\"name\":\"%s\"}}",
                     g_state.pid, proc_name);
        std::fputs(",\n", g_state.fp);
        std::fprintf(g_state.fp,
                     "{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":%d,\"tid\":%" PRIu64 ",\"args\":{\"name\":\"main\"}}",
                     g_state.pid, tid);
        std::fflush(g_state.fp);
    }
}

SR_NO_INSTRUMENT void shutdown() {
    if (!g_state.inited.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lock(g_state.mtx);
    if (g_state.fp) {
        write_footer_unlocked();
        std::fclose(g_state.fp);
        g_state.fp = nullptr;
    }
}

} // namespace srtrace

extern "C" {

SR_NO_INSTRUMENT void __cyg_profile_func_enter(void* this_fn, void* call_site) {
    (void)this_fn; (void)call_site; // fully disabled hook at runtime
    return;
}

SR_NO_INSTRUMENT void __cyg_profile_func_exit(void* this_fn, void* call_site) {
    (void)this_fn; (void)call_site; // fully disabled hook at runtime
    return;
}

}
