# Task 7: `AudioProcessor` - File Consolidation

**Objective:**
Consolidate all method implementations for the `AudioProcessor` class into `src/audio_engine/audio_processor.cpp`.

**Details:**

1.  **Identify Misplaced Methods:**
    *   The methods `AudioProcessor::updateSpeakerMix()`, `AudioProcessor::mixSpeakers()`, `AudioProcessor::splitBufferToChannels()`, and `AudioProcessor::mergeChannelsToBuffer()` are currently implemented in `src/audio_engine/speaker_mix.cpp`.

2.  **Move Implementations:**
    *   Cut the implementations of these methods from `src/audio_engine/speaker_mix.cpp`.
    *   Paste these implementations into `src/audio_engine/audio_processor.cpp`.
    *   Ensure the method signatures in `audio_processor.cpp` correctly use the `AudioProcessor::` scope qualifier.

3.  **Update Includes:**
    *   Ensure `src/audio_engine/audio_processor.cpp` includes any necessary headers that `speaker_mix.cpp` might have uniquely included for these methods (e.g., `<emmintrin.h>`, `<immintrin.h>` if SIMD intrinsics are used, though these are often included via `audio_processor.h` or other common headers).
    *   `speaker_mix.cpp` includes `audio_processor.h`. This dependency will be resolved by moving the code.

4.  **Cleanup `speaker_mix.cpp`:**
    *   After moving the methods, `src/audio_engine/speaker_mix.cpp` should be empty or contain only non-`AudioProcessor` related code (if any, which seems unlikely based on its current content).
    *   If `speaker_mix.cpp` becomes empty, it can be deleted from the project and build system.
    *   The `layout_mixer.cpp` and `layout_mixer.h` files seem to define a separate `layout_mixer` class and are distinct from `speaker_mix.cpp`'s `AudioProcessor` methods. This task only concerns `speaker_mix.cpp`.

**Acceptance Criteria:**

*   The implementations of `AudioProcessor::updateSpeakerMix()`, `AudioProcessor::mixSpeakers()`, `AudioProcessor::splitBufferToChannels()`, and `AudioProcessor::mergeChannelsToBuffer()` are located in `src/audio_engine/audio_processor.cpp`.
*   `src/audio_engine/speaker_mix.cpp` no longer contains these `AudioProcessor` method implementations.
*   If `src/audio_engine/speaker_mix.cpp` becomes empty as a result, it is removed from the build system and deleted.
*   The project compiles successfully, and all audio processing functionality related to speaker mixing and channel manipulation remains intact.
