// Function-level tracing to Chrome Trace Event JSON.
// Build-time enable using -finstrument-functions (CMake controls this).
// Runtime guard: set env var SCREAMROUTER_TRACE to enable writing.

#include "fntrace.h"

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>

#if !defined(_MSC_VER)
#  include <dlfcn.h>
#  include <sys/types.h>
#  include <unistd.h>
#  include <cxxabi.h>
#endif

namespace {

using SteadyClock = std::chrono::steady_clock;

struct TraceState {
    std::atomic<bool> inited{false};
    bool enabled{false};
    FILE* fp{nullptr};
    bool first_event{true};
    SteadyClock::time_point start;
    int pid{0};
    std::mutex mtx; // serialize writes
};

TraceState g_state;

#if defined(__GNUC__) || defined(__clang__)
#  define SR_NO_INSTRUMENT __attribute__((no_instrument_function))
#else
#  define SR_NO_INSTRUMENT
#endif

SR_NO_INSTRUMENT static uint64_t now_us() {
    auto d = std::chrono::duration_cast<std::chrono::microseconds>(SteadyClock::now() - g_state.start);
    return static_cast<uint64_t>(d.count());
}

SR_NO_INSTRUMENT static uint64_t thread_id_u64() {
    auto tid = std::this_thread::get_id();
    return static_cast<uint64_t>(std::hash<std::thread::id>{}(tid));
}

SR_NO_INSTRUMENT static std::string json_escape(const char* s) {
    std::string out;
    if (!s) return out;
    for (const unsigned char c : std::string(s)) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

SR_NO_INSTRUMENT static std::string demangle(const char* name) {
#if defined(_MSC_VER)
    return name ? name : "";
#else
    if (!name) return {};
    int status = 0;
    size_t len = 0;
    char* dem = abi::__cxa_demangle(name, nullptr, &len, &status);
    if (status == 0 && dem) {
        std::string out(dem);
        std::free(dem);
        return out;
    }
    if (dem) std::free(dem);
    return name;
#endif
}

SR_NO_INSTRUMENT static std::string symbol_name_from_addr(const void* addr) {
#if defined(_MSC_VER)
    (void)addr;
    return {};
#else
    Dl_info info{};
    if (dladdr(addr, &info) && info.dli_sname) {
        return demangle(info.dli_sname);
    }
    return {};
#endif
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

    g_state.start = SteadyClock::now();
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

SR_NO_INSTRUMENT bool is_enabled() { return g_state.enabled && g_state.fp != nullptr; }

SR_NO_INSTRUMENT void log_event_begin(const void* fn, const void* /*call_site*/) {
    if (!is_enabled()) return;
    const uint64_t ts = now_us();
    const uint64_t tid = thread_id_u64();
    std::string name = symbol_name_from_addr(fn);
    if (name.empty()) name = "unknown";
    std::string esc = json_escape(name.c_str());

    std::lock_guard<std::mutex> lock(g_state.mtx);
    if (!g_state.fp) return;
    if (!g_state.first_event) std::fputs(",\n", g_state.fp);
    g_state.first_event = false;
    std::fprintf(g_state.fp,
                 "{\"name\":\"%s\",\"cat\":\"audio_engine\",\"ph\":\"B\",\"ts\":%" PRIu64 ",\"pid\":%d,\"tid\":%" PRIu64 "}",
                 esc.c_str(), ts, g_state.pid, tid);
    std::fflush(g_state.fp);
}

SR_NO_INSTRUMENT void log_event_end(const void* /*fn*/, const void* /*call_site*/) {
    if (!is_enabled()) return;
    const uint64_t ts = now_us();
    const uint64_t tid = thread_id_u64();
    std::lock_guard<std::mutex> lock(g_state.mtx);
    if (!g_state.fp) return;
    if (!g_state.first_event) std::fputs(",\n", g_state.fp);
    g_state.first_event = false;
    std::fprintf(g_state.fp,
                 "{\"name\":\"\",\"cat\":\"audio_engine\",\"ph\":\"E\",\"ts\":%" PRIu64 ",\"pid\":%d,\"tid\":%" PRIu64 "}",
                 ts, g_state.pid, tid);
    std::fflush(g_state.fp);
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
