#ifndef Biquad_h
#define Biquad_h

#include <immintrin.h>

enum {
    bq_type_lowpass = 0,
    bq_type_highpass,
    bq_type_bandpass,
    bq_type_notch,
    bq_type_peak,
    bq_type_lowshelf,
    bq_type_highshelf
};

// Check for SSE support
#if defined(__SSE__) || defined(_M_IX86) || defined(_M_X64)
    #define BIQUAD_USE_SIMD 0
    //TODO: Fix or remove SIMD biquad
#else
    #define BIQUAD_USE_SIMD 0
#endif

class Biquad {
public:
    Biquad();
    Biquad(int type, double Fc, double Q, double peakGainDB);
    ~Biquad();
    void setType(int type);
    void setQ(double Q);
    void setFc(double Fc);
    void setPeakGain(double peakGainDB);
    void setBiquad(int type, double Fc, double Q, double peakGainDB);
    void flush(void);
    
    // Single sample processing
    float process(float in);
    
    // SIMD processing for 4 samples at once
    #if BIQUAD_USE_SIMD
    __m128 processSimd(__m128 in);
    #endif
    
    // Process a block of samples with SIMD if available, fallback to scalar otherwise
    void processBlock(float* input, float* output, int numSamples);
    
protected:
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

inline float Biquad::process(float in) {
    double out = in * a0 + z1;
    z1 = in * a1 + z2 - b1 * out;
    z2 = in * a2 - b2 * out;
    return out;
}

#if BIQUAD_USE_SIMD
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
