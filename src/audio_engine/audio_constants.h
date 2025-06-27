/**
 * @file audio_constants.h
 * @brief Defines constants used throughout the audio engine.
 * @details This file contains centralized constant values for audio processing,
 *          such as channel limits and EQ band counts, to ensure consistency
 *          across different modules.
 */
#ifndef AUDIO_CONSTANTS_H
#define AUDIO_CONSTANTS_H

namespace screamrouter {
/**
 * @namespace audio
 * @brief The main namespace for all audio processing components in ScreamRouter.
 */
namespace audio {

/**
 * @brief The maximum number of audio channels supported by the engine.
 * @details This constant is used to define the dimensions of matrices and buffers
 *          related to audio channels, such as the speaker mix matrix.
 */
constexpr int MAX_CHANNELS = 8;

/**
 * @brief The number of frequency bands in the equalizer.
 * @details This constant defines the size of arrays and vectors holding EQ gain values.
 */
constexpr int EQ_BANDS = 18;

} // namespace audio
} // namespace screamrouter

#endif // AUDIO_CONSTANTS_H
