# Task: Refactor `AudioProcessor`

**Overall Goal:**
Refactor `audio_processor.cpp` and `audio_processor.h` to align with the prevailing C++ style and practices observed in the `screamrouter::audio` namespace. This includes improvements to class structure (where applicable), configuration management, logging, memory management (RAII, smart pointers), naming conventions, and internal buffer handling (moving to `std::vector` with pre-allocation).

**Crucial Constraint:** No underlying audio processing algorithms or their sequence should be altered. Functionality must be preserved.

**Key Areas of Refactoring:**

1.  **Class Structure & Role:** Confirm and maintain its role as a utility class.
2.  **Configuration:** Introduce an `AudioProcessorConfig` struct and update the constructor.
3.  **Logging:** Remove the monitor thread and implement standardized logging macros.
4.  **Memory Management:** Replace raw pointers and C-style allocations with `std::unique_ptr` and custom deleters where necessary (e.g., for Biquad filters and `libsamplerate` states).
5.  **Code Style, Naming, and Constants:** Enforce consistent naming (trailing underscores for members), use `static constexpr` for constants, and ensure consistent formatting.
6.  **Internal Buffers:** Convert all C-style array member buffers to `std::vector`, pre-allocating them in the constructor to their maximum required sizes.
7.  **File Consolidation:** Move all `AudioProcessor` method implementations into `audio_processor.cpp`.
8.  **`isProcessingRequired` Cache:** Retain and restyle the caching mechanism.
9.  **Header File (`audio_processor.h`):** Update declarations to reflect all changes.

This refactoring aims to improve code consistency, maintainability, and adherence to modern C++ practices within the `AudioProcessor` component.
