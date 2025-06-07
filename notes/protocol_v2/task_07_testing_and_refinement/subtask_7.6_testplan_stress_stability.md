# Sub-Task 7.6: Test Plan for Stress and Stability

**Objective:** Define procedures for stress and stability testing of ScreamRouter with all new Protocol v2 features active, aiming to identify memory leaks, crashes, performance degradation, and audio glitches under sustained load and complex scenarios.

**Parent Task:** [Protocol v2 Testing and Refinement Strategy](../task_07_testing_and_refinement.md)
**Previous Sub-Task:** [Sub-Task 7.5: Test Plan for Backward Compatibility with Legacy Scream Protocol](./subtask_7.5_testplan_backward_compatibility.md)

## Tools:

*   ScreamRouter instance running.
*   Multiple instances of various client tools:
    *   Legacy Scream senders/receivers.
    *   RTP sender/receiver tools (VLC, GStreamer, `ortp-send`/`ortp-recv`).
    *   SIP clients capable of scripted registration/calls (e.g., `sipp`, custom `pjsua2` scripts).
    *   WebRTC test clients (multiple browser tabs/instances).
*   System monitoring tools:
    *   `top`/`htop` (Linux/macOS) or Task Manager (Windows) for CPU/memory usage.
    *   `valgrind` (memcheck tool) for detecting memory leaks in C++ components (requires building ScreamRouter with debug symbols and running it under valgrind).
    *   Python memory profilers (e.g., `memory_profiler`, `objgraph`) for Python components.
*   Network monitoring tools (Wireshark).
*   Logging: Verbose logging enabled in ScreamRouter (C++ and Python).
*   Scripting tools (Python, Bash) to automate client connections/disconnections and audio streaming.

## Test Scenarios:

1.  **TC-STRESS-001: High Number of Concurrent Connections**
    *   **Steps:**
        1.  Gradually increase the number of concurrently connected clients of mixed types:
            *   SIP registered devices (e.g., 10-50+).
            *   Active RTP streams (input and output, e.g., 5-20+).
            *   Active WebRTC client connections (e.g., 5-10+).
            *   Legacy Scream connections (e.g., 2-5).
        2.  Ensure all clients are actively sending/receiving audio or maintaining presence (SIP OPTIONS).
        3.  Maintain this load for an extended period (e.g., 1-4 hours).
    *   **Verification:**
        *   Monitor ScreamRouter's CPU and memory usage. Look for steady increases that don't plateau (indicating leaks).
        *   Check for crashes or unresponsiveness.
        *   Verify audio quality remains acceptable for all streams.
        *   Check logs for excessive errors, warnings, or resource exhaustion messages.
    *   **Pass/Fail.**

2.  **TC-STRESS-002: Connection Churn (Rapid Connect/Disconnect)**
    *   **Steps:**
        1.  Automate scripts to rapidly connect and disconnect clients of various types (SIP REGISTER/UNREGISTER, RTP stream start/stop, WebRTC session setup/teardown).
        2.  Simulate, for example, 100-1000 connection/disconnection cycles over 30-60 minutes.
        3.  Maintain a baseline of other stable connections during the churn.
    *   **Verification:**
        *   ScreamRouter handles rapid changes in client presence and stream states without crashing or leaking resources (sockets, threads, memory for sessions).
        *   No deadlocks or race conditions observed.
        *   Stable connections are not adversely affected.
    *   **Pass/Fail.**

3.  **TC-STRESS-003: Long Duration Run with Mixed Load**
    *   **Steps:**
        1.  Set up a moderate number of mixed-type clients (e.g., 5 SIP, 2 RTP in, 2 RTP out, 1 WebRTC, 1 legacy Scream in/out).
        2.  Let the system run continuously for an extended period (e.g., 12-24 hours or longer).
        3.  Periodically check audio quality and system responsiveness.
    *   **Verification:**
        *   No crashes or significant memory growth over the duration.
        *   Audio quality remains consistent.
        *   System remains responsive to new connections or configuration changes (if attempted during the run).
    *   **Pass/Fail.**

4.  **TC-STRESS-004: Network Instability Simulation (Optional, Advanced)**
    *   **Steps:**
        1.  Use network emulation tools (e.g., `tc` with `netem` on Linux) to introduce packet loss, latency, and jitter on the network interfaces used by ScreamRouter or clients.
        2.  Run mixed load tests (as in TC-STRESS-001 or TC-STRESS-003).
    *   **Verification:**
        *   ScreamRouter and its protocol handlers (RTP, WebRTC) attempt to cope with adverse network conditions gracefully.
        *   Observe audio quality degradation (expected) but check for crashes or unrecoverable states.
        *   Verify recovery when network conditions improve.
        *   (This tests the robustness of oRTP/libdatachannel jitter buffers and error concealment, and ScreamRouter's handling of library errors).
    *   **Pass/Fail.**

5.  **TC-STABILITY-001: Memory Leak Detection with Valgrind (C++)**
    *   **Steps:**
        1.  Compile ScreamRouter's C++ audio engine with debug symbols (`-g`).
        2.  Run ScreamRouter under `valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all python screamrouter.py`.
        3.  Perform a series of operations: connect/disconnect clients, stream audio, change configurations.
        4.  Shut down ScreamRouter gracefully.
    *   **Verification:**
        *   Valgrind report shows "definitely lost", "indirectly lost", or "possibly lost" blocks. Aim for zero "definitely lost" and "indirectly lost".
        *   Analyze any reported leaks to identify their source in C++ code (oRTP, libdatachannel usage, or custom code).
    *   **Pass/Fail.**

6.  **TC-STABILITY-002: Python Memory Profiling**
    *   **Steps:**
        1.  Use `memory_profiler` to decorate key Python methods in `SipManager`, `ConfigurationManager`, etc.
        2.  Run stress test scenarios (e.g., TC-STRESS-001, TC-STRESS-002).
        3.  Monitor memory usage reported by the profiler over time.
    *   **Verification:**
        *   No unbounded growth in memory usage for Python objects.
        *   Identify any Python objects that are created but not garbage collected appropriately (e.g., due to circular references not involving C++ extensions, or large data structures not being cleared).
    *   **Pass/Fail.**

## Monitoring and Reporting:

*   Continuously monitor system resource usage (CPU, RAM, network I/O, open file descriptors/sockets).
*   Collect and centralize logs from ScreamRouter and client tools.
*   Document all crashes with stack traces, core dumps (if enabled), and steps to reproduce.
*   Track memory usage trends over long runs.

## Acceptance Criteria:

*   ScreamRouter remains stable (no crashes) under high load and connection churn for specified durations.
*   Memory usage remains within acceptable limits and does not show unbounded growth over time.
*   CPU usage is reasonable for the given load.
*   Audio quality for active streams is maintained without significant glitches, drops, or distortion under normal load.
*   The system recovers gracefully from client disconnections.
*   Valgrind reports minimal or no "definitely lost" memory blocks for C++ components.
*   Python memory profiling shows no significant leaks in Python components.
