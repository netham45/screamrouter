#include <stdexcept>
#include <string>
#define MAX_CHANNELS 8
class layout_mixer {
public:
    float speaker_mix[MAX_CHANNELS][MAX_CHANNELS] = {{1}};
    int input_channels = 0;
    int output_channels = 0;
    
    // constructor to initialize the variables
    layout_mixer(int input_ch, int output_ch) : input_channels(input_ch), output_channels(output_ch) {
        if (input_ch > MAX_CHANNELS || output_ch > MAX_CHANNELS) {
            throw std::invalid_argument("Number of channels cannot exceed " + std::to_string(MAX_CHANNELS));
        }
        build_speaker_mix_table();
    }

    void build_speaker_mix_table()
    { // Fills out the speaker mix table speaker_mix[][] with the current configuration.
        memset(speaker_mix, 0, sizeof(speaker_mix));
        // speaker_mix[input channel][output channel] = gain;
        // Ex: To map Left on Stereo to Right on Stereo at half volume you would do:
        // speaker_mix[0][1] = .5;
        switch (input_channels)
        {
        case 1: // Mono, Ch 0: Left
            // Mono -> All
            for (int output_channel = 0; output_channel < MAX_CHANNELS; output_channel++) // Write the left (first) speaker to every channel
                speaker_mix[0][output_channel] = 1;
            break;
        case 2: // Stereo, Ch 0: Left, Ch 1: Right
            switch (output_channels)
            {
            case 1:                     // Stereo -> Mono
                speaker_mix[0][0] = .5; // Left to mono .5 vol
                speaker_mix[1][0] = .5; // Right to mono .5 vol
                break;
            case 2:                    // Stereo -> Stereo
                speaker_mix[0][0] = 1; // Left to Left
                speaker_mix[1][1] = 1; // Right to Right
                break;
            case 4:
                speaker_mix[0][0] = 1; // Left to Front Left
                speaker_mix[1][1] = 1; // Right to Front Right
                speaker_mix[0][2] = 1; // Left to Back Left
                speaker_mix[1][3] = 1; // Right to Back Right
                break;
            case 6: // Stereo -> 5.1 Surround
                // FL FR C LFE BL BR
                speaker_mix[0][0] = 1;  // Left to Front Left
                speaker_mix[0][5] = 1;  // Left to Rear Left
                speaker_mix[1][1] = 1;  // Right to Front Right
                speaker_mix[1][6] = 1;  // Right to Rear Right
                speaker_mix[0][3] = .5; // Left to Center Half Vol
                speaker_mix[1][3] = .5; // Right to Center Half Vol
                speaker_mix[0][4] = .5; // Right to Sub Half Vol
                speaker_mix[1][4] = .5; // Left to Sub Half Vol
                break;
            case 8: // Stereo -> 7.1 Surround
                // FL FR C LFE BL BR SL SR
                speaker_mix[0][0] = 1;  // Left to Front Left
                speaker_mix[0][6] = 1;  // Left to Side Left
                speaker_mix[0][4] = 1;  // Left to Rear Left
                speaker_mix[1][1] = 1;  // Right to Front Right
                speaker_mix[1][7] = 1;  // Right to Side Right
                speaker_mix[1][5] = 1;  // Right to Rear Right
                speaker_mix[0][2] = .5; // Left to Center Half Vol
                speaker_mix[1][2] = .5; // Right to Center Half Vol
                speaker_mix[0][3] = .5; // Right to Sub Half Vol
                speaker_mix[1][3] = .5; // Left to Sub Half Vol
                break;
            }
            break;
        case 4:
            switch (output_channels)
            {
            case 1:                      // Quad -> Mono
                speaker_mix[0][0] = .25; // Front Left to Mono
                speaker_mix[1][0] = .25; // Front Right to Mono
                speaker_mix[2][0] = .25; // Rear Left to Mono
                speaker_mix[3][0] = .25; // Rear Right to Mono
                break;
            case 2:                     // Quad -> Stereo
                speaker_mix[0][0] = .5; // Front Left to Left
                speaker_mix[1][1] = .5; // Front Right to Right
                speaker_mix[2][0] = .5; // Rear Left to Left
                speaker_mix[3][1] = .5; // Rear Right to Right
                break;
            case 4:
                speaker_mix[0][0] = 1; // Front Left to Front Left
                speaker_mix[1][1] = 1; // Front Right to Front Right
                speaker_mix[2][2] = 1; // Rear Left to Rear Left
                speaker_mix[3][3] = 1; // Rear Right to Rear Right
                break;
            case 6: // Quad -> 5.1 Surround
                // FL FR C LFE BL BR
                speaker_mix[0][0] = 1;   // Front Left to Front Left
                speaker_mix[1][1] = 1;   // Front Right to Front Right
                speaker_mix[0][2] = .5;  // Front Left to Center
                speaker_mix[1][2] = .5;  // Front Right to Center
                speaker_mix[0][3] = .25; // Front Left to LFE
                speaker_mix[1][3] = .25; // Front Right to LFE
                speaker_mix[2][3] = .25; // Rear Left to LFE
                speaker_mix[3][3] = .25; // Rear Right to LFE
                speaker_mix[2][4] = 1;   // Rear Left to Rear Left
                speaker_mix[3][5] = 1;   // Rear Right to Rear Right
                break;
            case 8: // Quad -> 7.1 Surround
                // FL FR C LFE BL BR SL SR
                speaker_mix[0][0] = 1;   // Front Left to Front Left
                speaker_mix[1][1] = 1;   // Front Right to Front Right
                speaker_mix[0][2] = .5;  // Front Left to Center
                speaker_mix[1][2] = .5;  // Front Right to Center
                speaker_mix[0][3] = .25; // Front Left to LFE
                speaker_mix[1][3] = .25; // Front Right to LFE
                speaker_mix[2][3] = .25; // Rear Left to LFE
                speaker_mix[3][3] = .25; // Rear Right to LFE
                speaker_mix[2][4] = 1;   // Rear Left to Rear Left
                speaker_mix[3][5] = 1;   // Rear Right to Rear Right
                speaker_mix[0][6] = .5;  // Front Left to Side Left
                speaker_mix[1][7] = .5;  // Front Right to Side Right
                speaker_mix[2][6] = .5;  // Rear Left to Side Left
                speaker_mix[3][7] = .5;  // Rear Right to Side Right
                break;
            }
            break;
        case 6:
            switch (output_channels)
            {
            case 1:                     // 5.1 Surround -> Mono
                speaker_mix[0][0] = .2; // Front Left to Mono
                speaker_mix[1][0] = .2; // Front Right to Mono
                speaker_mix[2][0] = .2; // Center to Mono
                speaker_mix[4][0] = .2; // Rear Left to Mono
                speaker_mix[5][0] = .2; // Rear Right to Mono
                break;
            case 2:                      // 5.1 Surround -> Stereo
                speaker_mix[0][0] = .33; // Front Left to Left
                speaker_mix[1][1] = .33; // Front Right to Right
                speaker_mix[2][0] = .33; // Center to Left
                speaker_mix[2][1] = .33; // Center to Right
                speaker_mix[4][0] = .33; // Rear Left to Left
                speaker_mix[5][1] = .33; // Rear Right to Right
                break;
            case 4:
                speaker_mix[0][0] = .66; // Front Left to Front Left
                speaker_mix[1][1] = .66; // Front Right to Front Right
                speaker_mix[2][0] = .33; // Center to Front Left
                speaker_mix[2][1] = .33; // Center to Front Right
                speaker_mix[4][2] = 1;   // Rear Left to Rear Left
                speaker_mix[5][3] = 1;   // Rear Right to Rear Right
                break;
            case 6: // 5.1 Surround -> 5.1 Surround
                // FL FR C LFE BL BR
                speaker_mix[0][0] = 1; // Front Left to Front Left
                speaker_mix[1][1] = 1; // Front Right to Front Right
                speaker_mix[2][2] = 1; // Center to Center
                speaker_mix[3][3] = 1; // LFE to LFE
                speaker_mix[4][4] = 1; // Rear Left to Rear Left
                speaker_mix[5][5] = 1; // Rear Right to Rear Right
                break;
            case 8: // 5.1 Surround -> 7.1 Surround
                // FL FR C LFE BL BR SL SR
                speaker_mix[0][0] = 1;  // Front Left to Front Left
                speaker_mix[1][1] = 1;  // Front Right to Front Right
                speaker_mix[2][2] = 1;  // Center to Center
                speaker_mix[3][3] = 1;  // LFE to LFE
                speaker_mix[4][4] = 1;  // Rear Left to Rear Left
                speaker_mix[5][5] = 1;  // Rear Right to Rear Right
                speaker_mix[0][6] = .5; // Front Left to Side Left
                speaker_mix[1][7] = .5; // Front Right to Side Right
                speaker_mix[4][6] = .5; // Rear Left to Side Left
                speaker_mix[5][7] = .5; // Rear Right to Side Right
                break;
            }
            break;
        case 8:
            switch (output_channels)
            {
            case 1:                              // 7.1 Surround -> Mono
                speaker_mix[0][0] = 1.0f / 7.0f; // Front Left to Mono
                speaker_mix[1][0] = 1.0f / 7.0f; // Front Right to Mono
                speaker_mix[2][0] = 1.0f / 7.0f; // Center to Mono
                speaker_mix[4][0] = 1.0f / 7.0f; // Rear Left to Mono
                speaker_mix[5][0] = 1.0f / 7.0f; // Rear Right to Mono
                speaker_mix[6][0] = 1.0f / 7.0f; // Side Left to Mono
                speaker_mix[7][0] = 1.0f / 7.0f; // Side Right to Mono
                break;
            case 2:                       // 7.1 Surround -> Stereo
                speaker_mix[0][0] = .5;   // Front Left to Left
                speaker_mix[1][1] = .5;   // Front Right to Right
                speaker_mix[2][0] = .25;  // Center to Left
                speaker_mix[2][1] = .25;  // Center to Right
                speaker_mix[4][0] = .125; // Rear Left to Left
                speaker_mix[5][1] = .125; // Rear Right to Right
                speaker_mix[6][0] = .125; // Side Left to Left
                speaker_mix[7][1] = .125; // Side Right to Right
                break;
            case 4:                      // 7.1 Surround -> Quad
                speaker_mix[0][0] = .5;  // Front Left to Front Left
                speaker_mix[1][1] = .5;  // Front Right to Front Right
                speaker_mix[2][0] = .25; // Center to Front Left
                speaker_mix[2][1] = .25; // Center to Front Right
                speaker_mix[4][2] = .66; // Rear Left to Rear Left
                speaker_mix[5][3] = .66; // Rear Right to Rear Right
                speaker_mix[6][0] = .25; // Side Left to Front Left
                speaker_mix[7][1] = .25; // Side Left to Front Right
                speaker_mix[6][2] = .33; // Side Left to Rear Left
                speaker_mix[7][3] = .33; // Side Left to Rear Right
                break;
            case 6: // 7.1 Surround -> 5.1 Surround
                // FL FR C LFE BL BR
                speaker_mix[0][0] = .66; // Front Left to Front Left
                speaker_mix[1][1] = .66; // Front Right to Front Right
                speaker_mix[2][2] = 1;   // Center to Center
                speaker_mix[3][3] = 1;   // LFE to LFE
                speaker_mix[4][4] = .66; // Rear Left to Rear Left
                speaker_mix[5][5] = .66; // Rear Right to Rear Right
                speaker_mix[6][0] = .33; // Side Left to Front Left
                speaker_mix[7][1] = .33; // Side Right to Front Right
                speaker_mix[6][4] = .33; // Side Left to Rear Left
                speaker_mix[7][5] = .33; // Side Right to Rear Right
                break;
            case 8: // 7.1 Surround -> 7.1 Surround
                // FL FR C LFE BL BR SL SR
                speaker_mix[0][0] = 1; // Front Left to Front Left
                speaker_mix[1][1] = 1; // Front Right to Front Right
                speaker_mix[2][2] = 1; // Center to Center
                speaker_mix[3][3] = 1; // LFE to LFE
                speaker_mix[4][4] = 1; // Rear Left to Rear Left
                speaker_mix[5][5] = 1; // Rear Right to Rear Right
                speaker_mix[6][6] = 1; // Side Left to Side Left
                speaker_mix[7][7] = 1; // Side Right to Side Right
                break;
            }
            break;
        }
    }

    void mix_speakers(int32_t** in_buffer, int32_t** remixed_out_buffer, int sample_count) {
        for (int pos = 0; pos < sample_count; pos++)
            for (int input_channel = 0; input_channel < input_channels; input_channel++)
                for (int output_channel = 0; output_channel < output_channels; output_channel++)
                        remixed_out_buffer[input_channel][output_channel] = 0;
        for (int pos = 0; pos < sample_count; pos++)
            for (int input_channel = 0; input_channel < input_channels; input_channel++)
                for (int output_channel = 0; output_channel < output_channels; output_channel++)
                    remixed_out_buffer[output_channel][pos] += in_buffer[input_channel][pos] * speaker_mix[input_channel][output_channel];
    }
};