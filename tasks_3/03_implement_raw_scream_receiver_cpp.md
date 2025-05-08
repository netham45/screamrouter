# Task 3: Implement RawScreamReceiver C++ File

**Goal**: Implement the functionality of the `RawScreamReceiver` class defined in the header file.

**Files to Create**:
*   `src/audio_engine/raw_scream_receiver.cpp`

**Steps**:

1.  **Create the file** `src/audio_engine/raw_scream_receiver.cpp`.
2.  **Include necessary headers**:
    *   `raw_scream_receiver.h`
    *   Standard C++ headers (`iostream`, `vector`, `cstring`, `chrono`, `stdexcept`, `system_error`, `utility`, `algorithm`).
3.  **Implement the Constructor**:
    *   Initialize members (`config_`, `notification_queue_`, `socket_fd_`).
    *   Validate `notification_queue_`.
    *   Add logging.
4.  **Implement the Destructor**:
    *   Ensure `stop()` is called.
    *   Join the `component_thread_` if joinable.
    *   Call `close_socket()`.
5.  **Implement `setup_socket()`**:
    *   Create UDP socket (`socket(AF_INET, SOCK_DGRAM, 0)`).
    *   Set socket options (e.g., `SO_REUSEADDR`).
    *   Prepare `sockaddr_in` for binding (`INADDR_ANY`, `config_.listen_port`).
    *   Bind the socket.
    *   Handle errors and return `true` or `false`.
6.  **Implement `close_socket()`**:
    *   Close `socket_fd_` if it's valid (`!= -1`).
    *   Set `socket_fd_` back to -1.
7.  **Implement `start()`**:
    *   Check if already running.
    *   Reset `stop_flag_`.
    *   Call `setup_socket()`. Return or throw if it fails.
    *   Launch `component_thread_` executing `run()`. Handle thread creation errors.
8.  **Implement `stop()`**:
    *   Check `stop_flag_`.
    *   Set `stop_flag_` to true.
    *   Call `close_socket()` (to potentially interrupt blocking calls in `run`).
    *   Join `component_thread_`. Handle join errors.
9.  **Implement `add_output_queue()`**:
    *   Acquire lock on `targets_mutex_`.
    *   Validate input pointers (`queue`, `processor_mutex`, `processor_cv`).
    *   Add/update the `SourceOutputTarget` in the `output_targets_` map using `source_tag` and `instance_id`.
    *   Add logging.
10. **Implement `remove_output_queue()`**:
    *   Acquire lock on `targets_mutex_`.
    *   Find the entry for `source_tag`.
    *   If found, find the entry for `instance_id` within the inner map.
    *   If found, erase the `instance_id` entry.
    *   If the inner map becomes empty, erase the `source_tag` entry.
    *   Add logging.
11. **Implement `is_valid_raw_scream_packet()`**:
    *   Check if `size` equals `SCREAM_HEADER_SIZE + CHUNK_SIZE` (e.g., 5 + 1152 = 1157).
    *   Optionally add checks for header byte values if there are known constraints.
    *   Return `true` or `false`.
12. **Implement `run()`**:
    *   **Loop**: `while (!stop_flag_)`.
    *   **Setup**: Create `receive_buffer`, `sockaddr_in client_addr`, `pollfd`.
    *   **Wait**: Use `poll()` with a timeout (`POLL_TIMEOUT_MS`) to wait for incoming data (`POLLIN`) on `socket_fd_`. Handle poll errors (`EINTR`).
    *   **Receive**: If `poll` indicates data is ready, call `recvfrom()`. Handle errors.
    *   **Validate**: Call `is_valid_raw_scream_packet()` on the received data.
    *   **Process Valid Packet**:
        *   Get `source_tag` (IP address) from `client_addr`.
        *   Get `received_time`.
        *   Check if `source_tag` is new using `known_source_tags_` (protected by `known_tags_mutex_`). If new, add it and push `NewSourceNotification` to `notification_queue_`.
        *   Create `TaggedAudioPacket`:
            *   `source_tag` = sender IP.
            *   `received_time` = current time.
            *   `audio_data` = copy the *entire* received payload (1157 bytes) from `receive_buffer`.
        *   Get a copy of the relevant `instance_targets` map for this `source_tag` (protected by `targets_mutex_`).
        *   Iterate through the copied instance targets:
            *   Create a *copy* of the `TaggedAudioPacket`.
            *   Push the copy to the target instance's `queue`.
            *   Notify the target instance's condition variable (`processor_cv`) using its mutex (`processor_mutex`), potentially using `try_lock`.
    *   **Handle Invalid Packet**: Log a warning/error.
    *   **Handle Socket Error**: If `poll` indicates an error (`POLLERR`, etc.), log error and potentially break the loop.
    *   **Cleanup**: Log loop exit.

**(Note: This implementation will be very similar to `RtpReceiver::run`, but the packet validation and the content of `TaggedAudioPacket::audio_data` will differ.)**
