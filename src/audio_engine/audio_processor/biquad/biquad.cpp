#include <math.h>
#include "biquad.h"

#define PI 3.14159265358979323846

Biquad::Biquad() {
    type = bq_type_lowpass;
    a0 = 1.0;
    a1 = a2 = b1 = b2 = 0.0;
    Fc = 0.50;
    Q = 0.707;
    peakGain = 0.0;
    z1 = z2 = 0.0;
    
    #if BIQUAD_USE_SIMD
    a0_simd = _mm_set1_ps(1.0f);
    a1_simd = a2_simd = b1_simd = b2_simd = _mm_set1_ps(0.0f);
    z1_simd = z2_simd = _mm_set1_ps(0.0f);
    #endif
}

Biquad::Biquad(int type, double Fc, double Q, double peakGainDB) {
    setBiquad(type, Fc, Q, peakGainDB);
    z1 = z2 = 0.0;
    
    #if BIQUAD_USE_SIMD
    z1_simd = z2_simd = _mm_set1_ps(0.0f);
    #endif
}

Biquad::~Biquad() {
}

void Biquad::setPeakGain(double peakGainDB) {
    this->peakGain = peakGainDB;
    calcBiquad();
}
    
void Biquad::setBiquad(int type, double Fc, double Q, double peakGainDB) {
    this->type = type;
    this->Q = Q;
    this->Fc = Fc;
    setPeakGain(peakGainDB);
}

void Biquad::flush(void) {
    z1 = z2 = 0.0;
    #if BIQUAD_USE_SIMD
    z1_simd = z2_simd = _mm_set1_ps(0.0f);
    #endif
}

void Biquad::calcBiquad(void) {
    double norm;
    double V = pow(10, fabs(peakGain) / 20.0);
    double K = tan(PI * Fc);
    switch (this->type) {
        case bq_type_lowpass:
            norm = 1 / (1 + K / Q + K * K);
            a0 = K * K * norm;
            a1 = 2 * a0;
            a2 = a0;
            b1 = 2 * (K * K - 1) * norm;
            b2 = (1 - K / Q + K * K) * norm;
            break;
            
        case bq_type_highpass:
            norm = 1 / (1 + K / Q + K * K);
            a0 = 1 * norm;
            a1 = -2 * a0;
            a2 = a0;
            b1 = 2 * (K * K - 1) * norm;
            b2 = (1 - K / Q + K * K) * norm;
            break;
            
        case bq_type_bandpass:
            norm = 1 / (1 + K / Q + K * K);
            a0 = K / Q * norm;
            a1 = 0;
            a2 = -a0;
            b1 = 2 * (K * K - 1) * norm;
            b2 = (1 - K / Q + K * K) * norm;
            break;
            
        case bq_type_notch:
            norm = 1 / (1 + K / Q + K * K);
            a0 = (1 + K * K) * norm;
            a1 = 2 * (K * K - 1) * norm;
            a2 = a0;
            b1 = a1;
            b2 = (1 - K / Q + K * K) * norm;
            break;
            
        case bq_type_peak:
            if (peakGain >= 0) {    // boost
                norm = 1 / (1 + 1/Q * K + K * K);
                a0 = (1 + V/Q * K + K * K) * norm;
                a1 = 2 * (K * K - 1) * norm;
                a2 = (1 - V/Q * K + K * K) * norm;
                b1 = a1;
                b2 = (1 - 1/Q * K + K * K) * norm;
            }
            else {    // cut
                norm = 1 / (1 + V/Q * K + K * K);
                a0 = (1 + 1/Q * K + K * K) * norm;
                a1 = 2 * (K * K - 1) * norm;
                a2 = (1 - 1/Q * K + K * K) * norm;
                b1 = a1;
                b2 = (1 - V/Q * K + K * K) * norm;
            }
            break;
        case bq_type_lowshelf:
            if (peakGain >= 0) {    // boost
                norm = 1 / (1 + sqrt(2) * K + K * K);
                a0 = (1 + sqrt(2*V) * K + V * K * K) * norm;
                a1 = 2 * (V * K * K - 1) * norm;
                a2 = (1 - sqrt(2*V) * K + V * K * K) * norm;
                b1 = 2 * (K * K - 1) * norm;
                b2 = (1 - sqrt(2) * K + K * K) * norm;
            }
            else {    // cut
                norm = 1 / (1 + sqrt(2*V) * K + V * K * K);
                a0 = (1 + sqrt(2) * K + K * K) * norm;
                a1 = 2 * (K * K - 1) * norm;
                a2 = (1 - sqrt(2) * K + K * K) * norm;
                b1 = 2 * (V * K * K - 1) * norm;
                b2 = (1 - sqrt(2*V) * K + V * K * K) * norm;
            }
            break;
        case bq_type_highshelf:
            if (peakGain >= 0) {    // boost
                norm = 1 / (1 + sqrt(2) * K + K * K);
                a0 = (V + sqrt(2*V) * K + K * K) * norm;
                a1 = 2 * (K * K - V) * norm;
                a2 = (V - sqrt(2*V) * K + K * K) * norm;
                b1 = 2 * (K * K - 1) * norm;
                b2 = (1 - sqrt(2) * K + K * K) * norm;
            }
            else {    // cut
                norm = 1 / (V + sqrt(2*V) * K + K * K);
                a0 = (1 + sqrt(2) * K + K * K) * norm;
                a1 = 2 * (K * K - 1) * norm;
                a2 = (1 - sqrt(2) * K + K * K) * norm;
                b1 = 2 * (K * K - V) * norm;
                b2 = (V - sqrt(2*V) * K + K * K) * norm;
            }
            break;
    }
    
    #if BIQUAD_USE_SIMD
    // Update SIMD coefficients
    a0_simd = _mm_set1_ps((float)a0);
    a1_simd = _mm_set1_ps((float)a1);
    a2_simd = _mm_set1_ps((float)a2);
    b1_simd = _mm_set1_ps((float)b1);
    b2_simd = _mm_set1_ps((float)b2);
    #endif
    
    return;
}

void Biquad::processBlock(float* input, float* output, int numSamples) {
    #if BIQUAD_USE_SIMD
    // Process in chunks of 4 samples using SIMD
    int i = 0;
    
    // Process aligned blocks of 4 samples
    for (; i <= numSamples - 4; i += 4) {
        __m128 in = _mm_loadu_ps(&input[i]);
        __m128 out = processSimd(in);
        _mm_storeu_ps(&output[i], out);
    }
    
    // Process remaining samples individually
    for (; i < numSamples; i++) {
        output[i] = process(input[i]);
    }
    #else
    // Fallback to scalar processing
    for (int i = 0; i < numSamples; i++) {
        output[i] = process(input[i]);
    }
    #endif
}
