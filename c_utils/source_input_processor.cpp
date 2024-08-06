// C++ code for taking in Scream audio data and converting it to a standard 32-bit PCM to be sent to the mixer

#include <iostream>
#include <vector>
#include "string.h"
#include <istream>
#include <unistd.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include "libsamplerate/include/samplerate.h"
#include "biquad/biquad.h"
#include <emmintrin.h>
#include <immintrin.h>
using namespace std;

// Configuration variables
#define CHUNK_SIZE 1152                        // Chunk size in bytes for audio data
#define HEADER_SIZE 5                          // Size of the Scream header, which contains information about the output audio stream
#define PACKET_SIZE (CHUNK_SIZE + HEADER_SIZE) // Total packet size including header and chunk
#define MAX_CHANNELS 8
#define EQ_BANDS 18
#define TAG_SIZE 45
#define NORMALIZE_EQ_GAIN 1

bool running = true; // Flag to control loop execution

uint8_t packet_in_buffer[PACKET_SIZE + TAG_SIZE];
uint8_t receive_buffer[CHUNK_SIZE];
int32_t scaled_buffer[CHUNK_SIZE * 8];
uint8_t *scaled_buffer_int8 = (uint8_t *)scaled_buffer;

int32_t processed_buffer[CHUNK_SIZE * 8];
uint8_t *processed_buffer_int8 = (uint8_t *)processed_buffer;

int32_t resampled_buffer[CHUNK_SIZE * 16];
uint8_t *resampled_buffer_int8 = (uint8_t *)resampled_buffer;

int32_t channel_buffers[MAX_CHANNELS][CHUNK_SIZE];
int32_t remixed_channel_buffers[MAX_CHANNELS][CHUNK_SIZE];
int32_t resampled_channel_buffers[MAX_CHANNELS][CHUNK_SIZE * 2];
int32_t pre_eq_buffer[MAX_CHANNELS][CHUNK_SIZE * 1024] = {0};
int32_t post_eq_buffer[MAX_CHANNELS][CHUNK_SIZE * 1024] = {0};
int scale_buffer_pos = 0;
int process_buffer_pos = 0;
int resample_buffer_pos = 0;
int channel_buffer_pos = 0;
float speaker_mix[MAX_CHANNELS][MAX_CHANNELS] = {{0}};

SRC_STATE *sampler = NULL;
float resampler_data_in[CHUNK_SIZE * MAX_CHANNELS] = {0};
float resampler_data_out[CHUNK_SIZE * MAX_CHANNELS * 8] = {0};
SRC_DATA resampler_config = {0};

string input_ip = "";

int fd_in = 0;
int fd_out = 0;

float eq[18] = {1};

float volume = 1;

int output_channels = 0;
int output_samplerate = 0;
int output_chlayout1 = 0;
int output_chlayout2 = 0;

int delay = 0;

uint8_t input_header[5] = {0};

int input_channels = 0;
int input_samplerate = 0;
int input_bitdepth = 0;
int input_chlayout1 = 0;
int input_chlayout2 = 0;

Biquad *filters[MAX_CHANNELS][EQ_BANDS] = {0};

// Array to hold integers passed from command line arguments, NULL indicates an argument is ignored
int *int_args[] = {
    NULL, // IP to receive from
    &fd_in,
    &fd_out,
    &output_channels,
    &output_samplerate,
    &output_chlayout1,
    &output_chlayout2,
    NULL, // volume
    NULL, // eq 1
    NULL, // ...
    NULL, // ...
    NULL, // ...
    NULL, // ...
    NULL, // ...
    NULL, // ...
    NULL, // ...
    NULL, // ...
    NULL, // ...
    NULL, // ...
    NULL, // ...
    NULL, // ...
    NULL, // ...
    NULL, // ...
    NULL, // ...
    NULL, // ...
    NULL, // eq 18
    &delay,
};

// Array to hold integers passed from command line arguments, NULL indicates an argument is ignored
float *float_args[] = {
    NULL, // IP to receive from
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    &volume,
    &eq[0],
    &eq[1],
    &eq[2],
    &eq[3],
    &eq[4],
    &eq[5],
    &eq[6],
    &eq[7],
    &eq[8],
    &eq[9],
    &eq[10],
    &eq[11],
    &eq[12],
    &eq[13],
    &eq[14],
    &eq[15],
    &eq[16],
    &eq[17],
    NULL};
int config_argc = sizeof(int_args) / sizeof(int *); // Number of command line arguments to process

void log(const string& message)
{
    cerr << "[Source Input Processor]" << message << endl;
}

// Function to process fixed command line arguments like IP address and output port
void process_args(int argc, char *argv[])
{
    if (argc <= config_argc) {
        log("Too few args");
        ::exit(-1);
    }
    // cppcheck-suppress ctuArrayIndex
    input_ip = string(argv[1]);
    for (int argi = 0; argi < config_argc; argi++)
        if (int_args[argi] == NULL)
            continue;
        else
            *(int_args[argi]) = atoi(argv[argi + 1]);
    for (int argi = 0; argi < config_argc; argi++)
        if (float_args[argi] == NULL)
            continue;
        else
            *(float_args[argi]) = atof(argv[argi + 1]);
}

float get_biquad_band_db(int i)
{ // return 0f<->2f mapped to -10f<->10f for the specified eq index
    return 10.0f * (eq[i] - 1);
}

void setup_biquad()
{ // Set up the biquad filters for the equalizer, sets filters
#ifdef NORMALIZE_EQ_GAIN
    float max_gain = 0;
    for (int i=0;i<EQ_BANDS;i++)
        if (eq[i] > max_gain)
            max_gain = eq[i];
    for (int i=0;i<EQ_BANDS;i++)
        eq[i] /= max_gain;
#endif
    for (int channel = 0; channel < MAX_CHANNELS; channel++)
    {
        filters[channel][0] = new Biquad(bq_type_peak, 65.406392 / output_samplerate, 1.0, get_biquad_band_db(0));
        filters[channel][1] = new Biquad(bq_type_peak, 92.498606 / output_samplerate, 1.0, get_biquad_band_db(1));
        filters[channel][2] = new Biquad(bq_type_peak, 130.81278 / output_samplerate, 1.0, get_biquad_band_db(2));
        filters[channel][3] = new Biquad(bq_type_peak, 184.99721 / output_samplerate, 1.0, get_biquad_band_db(3));
        filters[channel][4] = new Biquad(bq_type_peak, 261.62557 / output_samplerate, 1.0, get_biquad_band_db(4));
        filters[channel][5] = new Biquad(bq_type_peak, 369.99442 / output_samplerate, 1.0, get_biquad_band_db(5));
        filters[channel][6] = new Biquad(bq_type_peak, 523.25113 / output_samplerate, 1.0, get_biquad_band_db(6));
        filters[channel][7] = new Biquad(bq_type_peak, 739.9884 / output_samplerate, 1.0, get_biquad_band_db(7));
        filters[channel][8] = new Biquad(bq_type_peak, 1046.5023 / output_samplerate, 1.0, get_biquad_band_db(8));
        filters[channel][9] = new Biquad(bq_type_peak, 1479.9768 / output_samplerate, 1.0, get_biquad_band_db(9));
        filters[channel][10] = new Biquad(bq_type_peak, 2093.0045 / output_samplerate, 1.0, get_biquad_band_db(10));
        filters[channel][11] = new Biquad(bq_type_peak, 2959.9536 / output_samplerate, 1.0, get_biquad_band_db(11));
        filters[channel][12] = new Biquad(bq_type_peak, 4186.0091 / output_samplerate, 1.0, get_biquad_band_db(12));
        filters[channel][13] = new Biquad(bq_type_peak, 5919.9072 / output_samplerate, 1.0, get_biquad_band_db(13));
        filters[channel][14] = new Biquad(bq_type_peak, 8372.0181 / output_samplerate, 1.0, get_biquad_band_db(14));
        filters[channel][15] = new Biquad(bq_type_peak, 11839.814 / output_samplerate, 1.0, get_biquad_band_db(15));
        filters[channel][16] = new Biquad(bq_type_peak, 16744.036 / output_samplerate, 1.0, get_biquad_band_db(16));
        filters[channel][17] = new Biquad(bq_type_peak, 20000.0 / output_samplerate, 1.0, get_biquad_band_db(17));
    }
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

void initialize_sampler()
{ // Initialize sampler based on the number of input channels
    if (sampler)
        src_delete(sampler);
    int error = 0;
    sampler = src_new(0, input_channels, &error);
}

void check_update_header()
{ // Check the header in packet_in_buffer, if it's changed then figure out how to do the speaker mix and reinitialize the biquad filters and sampler
    if (memcmp(input_header, packet_in_buffer + TAG_SIZE, HEADER_SIZE) != 0)
    {
        log("Got new header");
        memcpy(input_header, packet_in_buffer + TAG_SIZE, HEADER_SIZE);
        input_samplerate = (input_header[0] & 0x7F) * ((input_header[0] & 0x80) ? 44100 : 48000);
        input_bitdepth = input_header[1];
        input_channels = input_header[2];
        input_chlayout1 = input_header[3];
        input_chlayout2 = input_header[4];
        log("Sample Rate: " + to_string(input_samplerate) + " -> " + to_string(output_samplerate));
        log("Bit Depth: " + to_string(input_bitdepth) + " -> 32");
        log("Channels: " + to_string(input_channels) + " -> " + to_string(output_channels));
        build_speaker_mix_table();
        initialize_sampler();
    }
}

void receive_data()
{ // Receive data from input fd and write it to receive_buffer, check the header
    while (int bytes = read(fd_in, packet_in_buffer, TAG_SIZE + PACKET_SIZE) != TAG_SIZE + PACKET_SIZE ||
           strcmp(input_ip.c_str(), reinterpret_cast<const char*>(packet_in_buffer)) != 0)
        if (bytes == -1)
            ::exit(-1);
    memcpy(receive_buffer, packet_in_buffer + HEADER_SIZE + TAG_SIZE, CHUNK_SIZE);
}

void scale_buffer()
{ // Scales receive_buffer to 32-bit and puts it in scaled_buffer
    scale_buffer_pos = 0;
    if (input_bitdepth != 16 && input_bitdepth != 24 && input_bitdepth != 32)
        return;
    for (int i = 0; i < CHUNK_SIZE; i += input_bitdepth / 8)
    {
        int scale_buffer_uint8_pos = scale_buffer_pos * sizeof(int32_t);
        switch (input_bitdepth)
        {
        case 16:
            scaled_buffer_int8[scale_buffer_uint8_pos + 0] = 0;
            scaled_buffer_int8[scale_buffer_uint8_pos + 1] = 0;
            scaled_buffer_int8[scale_buffer_uint8_pos + 2] = receive_buffer[i];
            scaled_buffer_int8[scale_buffer_uint8_pos + 3] = receive_buffer[i + 1];
            break;
        case 24:
            scaled_buffer_int8[scale_buffer_uint8_pos + 0] = 0;
            scaled_buffer_int8[scale_buffer_uint8_pos + 1] = receive_buffer[i];
            scaled_buffer_int8[scale_buffer_uint8_pos + 2] = receive_buffer[i + 1];
            scaled_buffer_int8[scale_buffer_uint8_pos + 3] = receive_buffer[i + 2];
            break;
        case 32:
            scaled_buffer_int8[scale_buffer_uint8_pos + 0] = receive_buffer[i];
            scaled_buffer_int8[scale_buffer_uint8_pos + 1] = receive_buffer[i + 1];
            scaled_buffer_int8[scale_buffer_uint8_pos + 2] = receive_buffer[i + 2];
            scaled_buffer_int8[scale_buffer_uint8_pos + 3] = receive_buffer[i + 3];
            break;
        }
        scale_buffer_pos++;
    }
}

void resample() 
{ // Resamples the daata in scaled_buffer from input_samplerate to output_samplerate and stores it in resampled_buffer
    if (input_samplerate != output_samplerate) {

        if (sampler == NULL)
            initialize_sampler();
        int datalen = scale_buffer_pos / input_channels;
        src_int_to_float_array(scaled_buffer, resampler_data_in, datalen * input_channels);
        resampler_config.data_in = resampler_data_in;
        resampler_config.data_out = resampler_data_out;
        resampler_config.src_ratio = (float)output_samplerate / (float)input_samplerate;
        resampler_config.input_frames = datalen;
        resampler_config.output_frames = datalen * 2;
        int err = src_process(sampler, &resampler_config);
        if (err != 0)
            log("Error: " + string(src_strerror(err)));
        src_float_to_int_array(resampler_data_out, resampled_buffer, resampler_config.output_frames_gen * input_channels);
        resample_buffer_pos = resampler_config.output_frames_gen * input_channels;
    } else {
        memcpy(resampled_buffer, scaled_buffer, scale_buffer_pos * sizeof(int32_t));
        resample_buffer_pos = scale_buffer_pos;
    }
}

void volume_buffer()
{ // Adjusts the volume of scaled_buffer
    for (int i = 0; i < scale_buffer_pos; i++)
        scaled_buffer[i] = scaled_buffer[i] * volume;
}

void split_buffer_to_channels()
{ // Separates resampled_buffer out into channels and stores in channel_buffers[channel][]
        for (int i=0;i<resample_buffer_pos; i++) {
        int channel = i % input_channels;
        int pos = i / input_channels;
        channel_buffers[channel][pos] = resampled_buffer[i];
    }
    channel_buffer_pos = scale_buffer_pos / input_channels;
}

void mix_speakers()
{ // Handles mixing audio data in channel_buffers and writing it to remixed_channel_buffers
    memset(remixed_channel_buffers, 0, sizeof(remixed_channel_buffers));
    
    #ifdef __AVX2__
    // AVX2 implementation
    for (int pos = 0; pos < channel_buffer_pos; pos += 8) {
        for (int input_channel = 0; input_channel < input_channels; input_channel++) {
            __m256i input_data = _mm256_loadu_si256((__m256i*)&channel_buffers[input_channel][pos]);
            for (int output_channel = 0; output_channel < output_channels; output_channel++) {
                __m256 mix_factor = _mm256_set1_ps(speaker_mix[input_channel][output_channel]);
                __m256 output_data = _mm256_cvtepi32_ps(_mm256_loadu_si256((__m256i*)&remixed_channel_buffers[output_channel][pos]));
                __m256 mixed_data = _mm256_fmadd_ps(_mm256_cvtepi32_ps(input_data), mix_factor, output_data);
                _mm256_storeu_si256((__m256i*)&remixed_channel_buffers[output_channel][pos], _mm256_cvtps_epi32(mixed_data));
            }
        }
    }
    #elif defined(__SSE2__)
    // SSE2 implementation
    for (int pos = 0; pos < channel_buffer_pos; pos += 4) {
        for (int input_channel = 0; input_channel < input_channels; input_channel++) {
            __m128i input_data = _mm_loadu_si128((__m128i*)&channel_buffers[input_channel][pos]);
            for (int output_channel = 0; output_channel < output_channels; output_channel++) {
                __m128 mix_factor = _mm_set1_ps(speaker_mix[input_channel][output_channel]);
                __m128 output_data = _mm_cvtepi32_ps(_mm_loadu_si128((__m128i*)&remixed_channel_buffers[output_channel][pos]));
                __m128 mixed_data = _mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(input_data), mix_factor), output_data);
                _mm_storeu_si128((__m128i*)&remixed_channel_buffers[output_channel][pos], _mm_cvtps_epi32(mixed_data));
            }
        }
    }
    #else
    // Fallback implementation
    for (int pos = 0; pos < channel_buffer_pos; pos++)
        for (int input_channel = 0; input_channel < input_channels; input_channel++)
            for (int output_channel = 0; output_channel < output_channels; output_channel++)
                remixed_channel_buffers[output_channel][pos] += channel_buffers[input_channel][pos] * speaker_mix[input_channel][output_channel];
    #endif
}
void equalize()
{ // Equalizes the buffer in remixed_channel_buffers and stores it in the same buffer
    for (int filter = 0; filter < EQ_BANDS; filter++)
        if (eq[filter] != 1.0f)
            for (int channel = 0; channel < output_channels; channel++)
                for (int pos = 0; pos < channel_buffer_pos; pos++)
                    remixed_channel_buffers[channel][pos] = filters[channel][filter]->process(remixed_channel_buffers[channel][pos]);
}

void merge_channels_to_buffer()
{ // Merges the buffers in remixed_channel_buffers into processed_buffer
    for (int channel = 0; channel < output_channels; channel++)
        for (int pos = 0; pos < channel_buffer_pos; pos++)
            processed_buffer[pos * output_channels + channel + process_buffer_pos] = remixed_channel_buffers[channel][pos];
    process_buffer_pos += channel_buffer_pos * output_channels;
}

void write_output_buffer()
{ // Sends the first CHUNK_SIZE in process_buffer, rotates process_buffer
    write(fd_out, processed_buffer_int8, CHUNK_SIZE);
    
    for (int pos=0;pos<sizeof(processed_buffer) - CHUNK_SIZE;pos++)
        processed_buffer_int8[pos] = processed_buffer_int8[pos+CHUNK_SIZE];
    process_buffer_pos -= CHUNK_SIZE / sizeof(int32_t);
}

int main(int argc, char *argv[])
{
    process_args(argc, argv);
    log("Starting source input processor " + input_ip);
    setup_biquad();
    while (running) {
        receive_data();
        check_update_header();
        scale_buffer();
        volume_buffer();
        resample();
        split_buffer_to_channels();
        mix_speakers();
        equalize();
        merge_channels_to_buffer();
        while (process_buffer_pos >= CHUNK_SIZE / sizeof(int32_t))
            write_output_buffer();
    }
    return 0;
}
