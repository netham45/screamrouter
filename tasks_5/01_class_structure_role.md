# Task 1: `AudioProcessor` - Class Structure & Role

**Objective:**
Confirm and maintain the `AudioProcessor`'s role as a utility class, not inheriting from `AudioComponent`.

**Details:**

*   The `AudioProcessor` class is currently designed as a self-contained audio processing unit. It is instantiated and used directly by other components like `SourceInputProcessor` and `SinkAudioMixer`.
*   It does not manage its own lifecycle thread in the same way `AudioComponent` derivatives do (e.g., `RtpReceiver`, `SinkAudioMixer`). Its processing is invoked synchronously via its `processAudio` method.

**Acceptance Criteria:**

*   The `AudioProcessor` class definition in `audio_processor.h` does not inherit from `screamrouter::audio::AudioComponent`.
*   The class remains a concrete utility class, directly usable by other parts of the audio engine.
*   No `start()`, `stop()`, or `run()` methods (in the `AudioComponent` sense) are added to `AudioProcessor`.
