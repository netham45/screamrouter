# Sub-Task 1.3: Implement RTP Packet Reception Loop in `RtpReceiver::run()`

**Objective:** Implement the `run()` method in `RtpReceiver` to continuously receive RTP packets using the oRTP session and pass them to a processing method.

**Parent Task:** [RTP Library (oRTP) Integration](../task_01_rtp_library_integration.md)
**Previous Sub-Task:** [Sub-Task 1.2: Define `RtpReceiver` Class Structure and oRTP Session Management](./subtask_1.2_define_rtp_receiver_class_structure.md)

## Key Steps & Considerations:

1.  **Override `run()` Method (`rtp_receiver.cpp`):**
    *   This method is inherited from `NetworkAudioReceiver` and will contain the main loop for packet reception.
    *   The loop should continue as long as `is_running_` (presumably a member of `NetworkAudioReceiver` or `RtpReceiver` itself, controlled by `start_receiving()`/`stop_receiving()`) is true.

2.  **Receiving Packets with oRTP:**
    *   Inside the loop, use `rtp_session_recv_with_ts(rtp_session_, 0)` to receive an RTP packet.
        *   The second argument `uint32_t user_ts` is the user timestamp, which is not strictly needed for basic reception if relying on RTP timestamps directly. Passing `0` or the current time might be appropriate. oRTP uses this for RTCP reporting.
        *   This function blocks until a packet is received or an error occurs.
    *   `rtp_session_recv_with_ts` returns an `mblk_t*` (message block pointer) containing the RTP packet.
    *   If `rtp_session_` is null or not initialized, the loop should not run or should exit.

3.  **Handling Received Data:**
    *   If `rtp_session_recv_with_ts` returns a valid `mblk_t*` (not `nullptr`):
        *   Call `process_received_packet(rtp_packet_block)` to handle the packet.
        *   The `mblk_t` structure is managed by oRTP. `process_received_packet` will be responsible for extracting data and then `freemsg(rtp_packet_block)` must be called to free the message block when done with it.
    *   If it returns `nullptr`, it could indicate an error or that the session was shut down.
        *   Check `ortp_get_error()` or `errno` for more details if an error is suspected.
        *   If `is_running_` is still true, it might be a non-fatal error, or the session might need to be checked.
        *   If `is_running_` became false (e.g., `stop_receiving()` was called), the loop should terminate.

4.  **Loop Control and Shutdown:**
    *   The `is_running_` flag (or similar mechanism like `thread_` from `NetworkAudioReceiver`) must be checked in each iteration to allow graceful shutdown.
    *   `rtp_session_recv_with_ts` can be interrupted if the underlying socket is closed. `rtp_session_destroy()` (called from `stop_receiving()` or destructor) should trigger this.
    *   Consider a small sleep or yield in the loop if `rtp_session_recv_with_ts` has a timeout option or if polling is used (though `rtp_session_recv_with_ts` is typically blocking). oRTP's scheduler handles much of the timing.

5.  **`RtpReceiver::run()` Implementation Sketch (`rtp_receiver.cpp`):**
    ```cpp
    // In src/audio_engine/rtp_receiver.cpp

    void RtpReceiver::run() {
        screamrouter_logger::info("RtpReceiver ({}) run() loop started for port {}.", source_id_, local_rtp_port_);
        
        if (!rtp_session_) {
            screamrouter_logger::error("RtpReceiver ({}): oRTP session is not initialized. Cannot run.", source_id_);
            return;
        }

        // Assuming is_running_ is a member of NetworkAudioReceiver or RtpReceiver,
        // and set to true by a start_receiving() method and false by stop_receiving().
        // Or, using the thread_ object from NetworkAudioReceiver if that's the pattern.
        // For this example, let's assume a simple is_running_ flag.
        // NetworkAudioReceiver::is_running_ = true; // This would be set by a start method

        while (is_running_.load()) { // Use atomic bool for is_running_
            mblk_t* rtp_packet_block = nullptr;
            
            // Set a timeout for the receive operation to allow checking is_running_ periodically
            // rtp_session_set_recv_timeout(rtp_session_, 100); // 100 ms timeout, for example
            // Note: oRTP's primary receive functions are blocking. Using timeouts might require
            // different oRTP APIs or careful handling of the scheduler.
            // A simpler approach for now is direct blocking recv_with_ts and relying on
            // session destruction to unblock.

            // The timestamp argument to rtp_session_recv_with_ts is for RTCP reporting.
            // We can pass a dummy value or 0 if not immediately using it.
            uint32_t current_time_ms = ortp_time_get_ms(); // Example, or just 0
            rtp_packet_block = rtp_session_recv_with_ts(rtp_session_, current_time_ms);

            if (rtp_packet_block != nullptr) {
                // Successfully received a packet
                process_received_packet(rtp_packet_block); 
                // process_received_packet is responsible for calling freemsg(rtp_packet_block)
            } else {
                // rtp_session_recv_with_ts returned NULL
                if (!is_running_.load()) {
                    screamrouter_logger::info("RtpReceiver ({}): Told to stop, exiting receive loop.", source_id_);
                    break; 
                }
                // An error might have occurred, or the session was remotely closed.
                // oRTP might log errors internally.
                // If it's a persistent error, we might need to break or re-initialize.
                // For now, we'll log and continue if is_running_ is still true,
                // assuming oRTP handles transient issues or session will be cleaned up.
                // screamrouter_logger::warn("RtpReceiver ({}): rtp_session_recv_with_ts returned NULL.", source_id_);
                // A small delay to prevent tight loop on persistent errors if not breaking
                std::this_thread::sleep_for(std::chrono::milliseconds(10)); 
            }
        }
        screamrouter_logger::info("RtpReceiver ({}) run() loop finished.", source_id_);
    }
    ```

6.  **`stop_receiving()` Implementation (`rtp_receiver.cpp`):**
    *   This method should set `is_running_` to `false`.
    *   It should then call `destroy_ortp_session()`. Destroying the session will typically close the underlying sockets, which should cause `rtp_session_recv_with_ts` in the `run()` loop to unblock (possibly returning `nullptr` or an error).
    *   The thread join logic (e.g., `if (thread_.joinable()) thread_.join();`) from `NetworkAudioReceiver`'s `stop_receiving` should then wait for the `run()` loop to exit.

    ```cpp
    // In src/audio_engine/rtp_receiver.cpp
    void RtpReceiver::stop_receiving() {
        screamrouter_logger::info("RtpReceiver ({}): stop_receiving() called.", source_id_);
        is_running_.store(false); // Signal the run loop to stop

        // Destroying the session should help unblock rtp_session_recv_with_ts
        destroy_ortp_session(); 

        // Call base class stop_receiving if it handles thread joining
        NetworkAudioReceiver::stop_receiving(); // This should join the thread_
    }
    ```

## Code Alterations:

*   **File:** `src/audio_engine/rtp_receiver.cpp`
    *   Implement the `run()` method as sketched above, including the loop, call to `rtp_session_recv_with_ts`, and call to `process_received_packet`.
    *   Implement/override `stop_receiving()` to set the running flag to false and destroy the oRTP session, then call the base method.
*   **File:** `src/audio_engine/rtp_receiver.h`
    *   Ensure `is_running_` (likely `std::atomic<bool> is_running_;`) is declared, probably in the base `NetworkAudioReceiver` or in `RtpReceiver` if not in base. Initialize to `false`.
    *   A `start_receiving()` method in `NetworkAudioReceiver` (or `RtpReceiver`) would set `is_running_ = true;` and launch the `thread_`.

## Recommendations:

*   **`is_running_` flag:** This should be an `std::atomic<bool>` if `stop_receiving()` can be called from a different thread than `run()`.
*   **Timeout for `rtp_session_recv_with_ts`:** oRTP's `rtp_session_recv_with_ts` is blocking. If a more responsive shutdown is needed without relying solely on session destruction, investigate oRTP's non-blocking APIs or how its scheduler interacts with `select()`/`poll()` on its file descriptors. For now, destroying the session is the most straightforward way to unblock it.
*   **Error Logging:** Add more detailed logging within the loop, especially for cases where `rtp_packet_block` is `nullptr` but `is_running_` is still true.
*   **`freemsg(rtp_packet_block)`:** Crucially, the `mblk_t` returned by oRTP must be freed using `freemsg()`. This responsibility will be delegated to `process_received_packet` or handled immediately after it returns.

## Acceptance Criteria:

*   The `RtpReceiver::run()` method contains a loop that calls `rtp_session_recv_with_ts`.
*   The loop correctly checks an `is_running_` flag for termination.
*   `stop_receiving()` effectively signals the `run()` loop to terminate and cleans up the oRTP session.
*   Received `mblk_t` packets are passed to `process_received_packet` (to be implemented next).
*   The system remains stable, and the receiver thread can be started and stopped cleanly.
