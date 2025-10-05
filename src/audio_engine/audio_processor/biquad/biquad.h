/**
 * @file biquad.h
 * @brief Defines the Biquad class for implementing a biquadratic filter.
 * @details This file contains the definition of the Biquad class, which can be configured
 *          to act as various types of second-order IIR filters (e.g., lowpass, highpass, peak).
 *          It includes an enumeration for filter types and supports both scalar and SIMD processing.
 */
#ifndef Biquad_h
#define Biquad_h

#include <immintrin.h>

/**
 * @enum
 * @brief Enumeration of the available biquad filter types.
 */
enum {
    bq_type_lowpass = 0,    ///< Low-pass filter.
    bq_type_highpass,   ///< High-pass filter.
    bq_type_bandpass,   ///< Band-pass filter.
    bq_type_notch,      ///< Notch filter.
    bq_type_peak,       ///< Peak filter.
    bq_type_lowshelf,   ///< Low-shelf filter.
    bq_type_highshelf   ///< High-shelf filter.
};

// Check for SSE support
#if defined(__SSE__) || defined(_M_IX86) || defined(_M_X64)
    /**
     * @def BIQUAD_USE_SIMD
     * @brief A macro to control whether SIMD instructions are used. Disabled for now.
     */
    #define BIQUAD_USE_SIMD 0
    //TODO: Fix or remove SIMD biquad
#else
    #define BIQUAD_USE_SIMD 0
#endif

/**
 * @class Biquad
 * @brief Implements a biquadratic (second-order IIR) filter.
 * @details This class can be configured to implement various types of filters. It supports
 *          processing single samples or blocks of samples, with an optional (currently disabled)
 *          SIMD implementation for performance.
 */
class Biquad {
public:
    /**
     * @brief Default constructor.
     */
    Biquad();
    /**
     * @brief Constructs a Biquad filter with specified parameters.
     * @param type The type of filter (e.g., bq_type_lowpass).
     * @param Fc The cutoff or center frequency.
     * @param Q The quality factor.
     * @param peakGainDB The gain in dB for peak and shelf filters.
     */
    Biquad(int type, double Fc, double Q, double peakGainDB);
    /**
     * @brief Destructor.
     */
    ~Biquad();
    /**
     * @brief Sets the filter type.
     * @param type The new filter type.
     */
    void setType(int type);
    /**
     * @brief Sets the quality factor (Q).
     * @param Q The new Q value.
     */
    void setQ(double Q);
    /**
     * @brief Sets the cutoff/center frequency.
     * @param Fc The new frequency in Hz.
     */
    void setFc(double Fc);
    /**
     * @brief Sets the peak gain for shelf and peak filters.
     * @param peakGainDB The new gain in decibels.
     */
    void setPeakGain(double peakGainDB);
    /**
     * @brief Sets all filter parameters at once.
     * @param type The filter type.
     * @param Fc The cutoff/center frequency.
     * @param Q The quality factor.
     * @param peakGainDB The peak gain in dB.
     */
    void setBiquad(int type, double Fc, double Q, double peakGainDB);
    /**
     * @brief Resets the filter's internal state (clears delay lines).
     */
    void flush(void);
    
    /**
     * @brief Processes a single audio sample.
     * @param in The input sample.
     * @return The processed output sample.
     */
    float process(float in);
    
    #if BIQUAD_USE_SIMD
    /**
     * @brief Processes four samples at once using SIMD instructions.
     * @param in A __m128 vector containing four input samples.
     * @return A __m128 vector containing the four processed samples.
     */
    __m128 processSimd(__m128 in);
    #endif
    
    /**
     * @brief Processes a block of audio samples.
     * @details This method will use SIMD instructions if enabled, otherwise it falls back to scalar processing.
     * @param input Pointer to the input buffer.
     * @param output Pointer to the output buffer.
     * @param numSamples The number of samples to process.
     */
    void processBlock(float* input, float* output, int numSamples);
    
protected:
    /**
     * @brief Calculates the filter coefficients based on the current parameters.
     */
    void calcBiquad(void);

    int type;
    double a0, a1, a2, b1, b2;
    double Fc, Q, peakGain;
    double z1, z2;
    
    #if BIQUAD_USE_SIMD
    // SIMD state variables
    __m128 a0_simd, a1_simd, a2_simd, b1_simd, b2_simd;
    __m128 z1_simd, z2_simd;
    #endif
};

/**
 * @brief Inline implementation of the single-sample processing method.
 * @param in The input sample.
 * @return The processed output sample.
 */
inline float Biquad::process(float in) {
    double out = in * a0 + z1;
    z1 = in * a1 + z2 - b1 * out;
    z2 = in * a2 - b2 * out;
    return out;
}

#if BIQUAD_USE_SIMD
/**
 * @brief Inline implementation of the SIMD processing method.
 * @param in A __m128 vector of four input samples.
 * @return A __m128 vector of four output samples.
 */
inline __m128 Biquad::processSimd(__m128 in) {
    // Process 4 samples at once using SSE
    __m128 out = _mm_add_ps(_mm_mul_ps(in, a0_simd), z1_simd);
    z1_simd = _mm_add_ps(_mm_add_ps(_mm_mul_ps(in, a1_simd), z2_simd),
                         _mm_mul_ps(_mm_mul_ps(out, b1_simd), _mm_set1_ps(-1.0f)));
    z2_simd = _mm_add_ps(_mm_mul_ps(in, a2_simd),
                         _mm_mul_ps(_mm_mul_ps(out, b2_simd), _mm_set1_ps(-1.0f)));
    return out;
}
#endif

#endif // Biquad_h
