# Task 4: Integrate RawScreamReceiver into AudioManager

**Goal**: Modify `AudioManager` to manage the lifecycle and configuration of `RawScreamReceiver` instances alongside the existing `RtpReceiver`.

**Files to Modify**:
*   `src/audio_engine/audio_manager.h`
*   `src/audio_engine/audio_manager.cpp`

**Steps**:

1.  **Include Header** (`audio_manager.h`):
    *   Include `raw_scream_receiver.h`.

2.  **Add Member Variable** (`audio_manager.h`):
    *   Add a map to store `RawScreamReceiver` instances, keyed by a unique ID (e.g., the listen port as a string, or a dedicated ID). Let's use listen port for simplicity initially.
    ```cpp
    // Raw Scream Receivers (Port -> Receiver Ptr) - Assuming one receiver per port
    std::map<int, std::unique_ptr<RawScreamReceiver>> raw_scream_receivers_;
    ```
    *   *(Self-correction: Using port as ID might be limiting if multiple receivers on different interfaces but same port are needed later. A dedicated ID string might be better, similar to sinks. Let's stick with port for now as per `c_utils` model but keep this in mind).*

3.  **Add Management Methods** (`audio_manager.h`):
    *   Declare public methods to add and remove raw Scream receivers.
    ```cpp
    /**
     * @brief Adds and starts a new raw Scream receiver.
     * @param config Configuration for the raw Scream receiver (e.g., listen port).
     * @return true if the receiver was added successfully, false otherwise.
     */
    bool add_raw_scream_receiver(const RawScreamReceiverConfig& config);

    /**
     * @brief Stops and removes an existing raw Scream receiver.
     * @param listen_port The listen port of the receiver to remove.
     * @return true if the receiver was removed successfully, false if not found.
     */
    bool remove_raw_scream_receiver(int listen_port);
    ```

4.  **Implement `add_raw_scream_receiver`** (`audio_manager.cpp`):
    *   Acquire `manager_mutex_`.
    *   Check if `running_`.
    *   Check if a receiver for `config.listen_port` already exists in `raw_scream_receivers_`. Return `false` if it does.
    *   Create a `std::unique_ptr<RawScreamReceiver>` using `std::make_unique`. Pass the `config` and the shared `new_source_notification_queue_`.
    *   Call `start()` on the new receiver instance.
    *   Check if `start()` was successful (e.g., using `is_running()` after a short delay). If not, log error, clean up, return `false`.
    *   Move the `unique_ptr` into the `raw_scream_receivers_` map.
    *   Log success and return `true`.

5.  **Implement `remove_raw_scream_receiver`** (`audio_manager.cpp`):
    *   Declare `std::unique_ptr<RawScreamReceiver> receiver_to_remove;` outside the lock scope.
    *   Acquire `manager_mutex_`.
    *   Check if `running_`.
    *   Find the receiver in `raw_scream_receivers_` using `listen_port`. Return `false` if not found.
    *   Move the found `unique_ptr` to `receiver_to_remove`.
    *   Erase the entry from the map.
    *   Release `manager_mutex_`.
    *   If `receiver_to_remove` is not null, call `stop()` on it.
    *   Log success and return `true`.

6.  **Update `initialize`** (`audio_manager.cpp`):
    *   No changes strictly needed here unless common setup is required for both receiver types. The notification queue is already created.

7.  **Update `shutdown`** (`audio_manager.cpp`):
    *   Add logic to stop and clear all `RawScreamReceiver` instances, similar to how `RtpReceiver` and other components are handled. Iterate through `raw_scream_receivers_`, call `stop()` on each, then `clear()` the map. Ensure this happens *after* stopping source processors but *before* clearing the notification queue.

8.  **Update `configure_source`** (`audio_manager.cpp`):
    *   *(Deferred to Task 7)* This method will need modification later to determine which receiver (`RtpReceiver` or `RawScreamReceiver`) the new `SourceInputProcessor` should get packets from and register the queue accordingly. For now, just ensure the existing logic for `RtpReceiver` isn't broken.

9.  **Update `remove_source`** (`audio_manager.cpp`):
    *   *(Deferred to Task 7)* This method will need modification later to remove the source's queue from the correct receiver type.
