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
#include "libsamplerate-0.1.9/src/samplerate.h"
using namespace std;

// Configuration variables
#define CHUNK_SIZE 1152 // Chunk size in bytes for audio data
#define HEADER_SIZE 5 // Size of the Scream header, which contains information about the output audio stream
#define PACKET_SIZE (CHUNK_SIZE + HEADER_SIZE) // Total packet size including header and chunk
#define MAX_CHANNELS 8

bool running = true; // Flag to control loop execution

uint8_t packet_in_buffer[PACKET_SIZE];
uint8_t receive_buffer[CHUNK_SIZE];
int32_t scaled_buffer[CHUNK_SIZE * 8];
uint8_t *scaled_buffer_int8 = (uint8_t*)scaled_buffer;
int32_t channel_buffers[MAX_CHANNELS][CHUNK_SIZE];
int32_t remixed_channel_buffers[MAX_CHANNELS][CHUNK_SIZE];
int32_t pre_eq_buffer[MAX_CHANNELS][CHUNK_SIZE * 1024] = {0};
int32_t post_eq_buffer[MAX_CHANNELS][CHUNK_SIZE * 1024] = {0};
int scale_buffer_uint8_pos = 0;
int channel_buffer_pos = 0;
float speaker_mix[MAX_CHANNELS][MAX_CHANNELS] = {{0}};

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

// Array to hold integers passed from command line arguments, NULL indicates an argument is ignored
int* int_args[] = { 
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
float* float_args[] = {
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
                        NULL
                        };
int config_argc = sizeof(int_args) / sizeof(int*); // Number of command line arguments to process

void log(string message) {
    cerr << "[Source Input Processor]" << message << endl;
}

// Function to process fixed command line arguments like IP address and output port
void process_base_args(int argc, char* argv[]) {
    if (argc <= config_argc)
        ::exit(-1);
    log("args");
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

void build_speaker_mix() {
    memset(speaker_mix, 0, sizeof(speaker_mix));
    // speaker_mix[input channel][output channel] = gain;
    // Ex: To map Left on Stereo to Right on Stereo at half volume you would do:
    // speaker_mix[0][1] = .5;
    switch(input_channels) {
        case 1: // Mono, Ch 0: Left
            // Mono -> All
            for (int output_channel=0;output_channel<MAX_CHANNELS;output_channel++) // Write the left (first) speaker to every channel
                speaker_mix[0][output_channel] = 1;
            break;
        case 2: // Stereo, Ch 0: Left, Ch 1: Right
            switch (output_channels) {
                case 1: // Stereo -> Mono
                    speaker_mix[0][0] = .5; // Left to mono .5 vol
                    speaker_mix[1][0] = .5; // Right to mono .5 vol
                    break;
                case 2: // Stereo -> Stereo
                    speaker_mix[0][0] = 1; // Left to Left
                    speaker_mix[1][1] = 1; // Right to Right
                    break;
                case 4: 
                    if (output_chlayout1 == 0x33 && output_chlayout2 == 0x00) { // Stereo -> Quad
                        speaker_mix[0][0] = 1; // Left to Front Left
                        speaker_mix[1][1] = 1; // Right to Front Right
                        speaker_mix[0][2] = 1; // Left to Back Left
                        speaker_mix[1][3] = 1; // Right to Back Right
                    }
                    break;
                case 6: // Stereo -> 5.1 Surround
                    // FL FR C LFE BL BR
                    speaker_mix[0][0] = 1; // Left to Front Left
                    speaker_mix[0][5] = 1; // Left to Rear Left
                    speaker_mix[1][1] = 1; // Right to Front Right
                    speaker_mix[1][6] = 1; // Right to Rear Right
                    speaker_mix[0][3] = .5; // Left to Center Half Vol
                    speaker_mix[1][3] = .5; // Right to Center Half Vol
                    speaker_mix[0][4] = .5; // Right to Sub Half Vol
                    speaker_mix[1][4] = .5; // Left to Sub Half Vol
                    break;
                case 8: // Stereo -> 7.1 Surround
                    // FL FR C LFE BL BR SL SR
                    speaker_mix[0][0] = 1; // Left to Front Left
                    speaker_mix[0][6] = 1; // Left to Side Left
                    speaker_mix[0][4] = 1; // Left to Rear Left
                    speaker_mix[1][1] = 1; // Right to Front Right
                    speaker_mix[1][7] = 1; // Right to Side Right
                    speaker_mix[1][5] = 1; // Right to Rear Right
                    speaker_mix[0][2] = .5; // Left to Center Half Vol
                    speaker_mix[1][2] = .5; // Right to Center Half Vol
                    speaker_mix[0][3] = .5; // Right to Sub Half Vol
                    speaker_mix[1][3] = .5; // Left to Sub Half Vol
                    break;
            }
            break;
        case 4:
            if (output_chlayout1 == 0x33 && output_chlayout2 == 0x00) { // Quad
                switch (output_channels) {
                    case 1: // Quad -> Mono
                        speaker_mix[0][0] = .25; // Front Left to Mono
                        speaker_mix[1][0] = .25; // Front Right to Mono
                        speaker_mix[2][0] = .25; // Rear Left to Mono
                        speaker_mix[3][0] = .25; // Rear Right to Mono
                        break;
                    case 2: // Quad -> Stereo
                        speaker_mix[0][0] = .5; // Front Left to Left
                        speaker_mix[1][1] = .5; // Front Right to Right
                        speaker_mix[2][0] = .5; // Rear Left to Left
                        speaker_mix[3][1] = .5; // Rear Right to Right
                        break;
                    case 4: 
                        if (output_chlayout1 == 0x33 && output_chlayout2 == 0x00) { // Quad -> Quad
                            speaker_mix[0][0] = 1; // Front Left to Front Left
                            speaker_mix[1][1] = 1; // Front Right to Front Right
                            speaker_mix[2][2] = 1; // Rear Left to Rear Left
                            speaker_mix[3][3] = 1; // Rear Right to Rear Right
                        }
                        break;
                    case 6: // Quad -> 5.1 Surround
                        // FL FR C LFE BL BR
                        speaker_mix[0][0] = 1; // Front Left to Front Left
                        speaker_mix[1][1] = 1; // Front Right to Front Right
                        speaker_mix[0][2] = .5; // Front Left to Center
                        speaker_mix[0][2] = .5; // Front Right to Center
                        speaker_mix[0][3] = .25; // Front Left to LFE
                        speaker_mix[1][3] = .25; // Front Right to LFE
                        speaker_mix[2][3] = .25; // Rear Left to LFE
                        speaker_mix[3][3] = .25; // Rear Right to LFE
                        speaker_mix[2][4] = 1; // Rear Left to Rear Left
                        speaker_mix[3][5] = 1; // Rear Right to Rear Right
                        break;
                    case 8: // Quad -> 7.1 Surround
                        // FL FR C LFE BL BR SL SR
                        speaker_mix[0][0] = 1; // Front Left to Front Left
                        speaker_mix[1][1] = 1; // Front Right to Front Right
                        speaker_mix[0][2] = .5; // Front Left to Center
                        speaker_mix[0][2] = .5; // Front Right to Center
                        speaker_mix[0][3] = .25; // Front Left to LFE
                        speaker_mix[1][3] = .25; // Front Right to LFE
                        speaker_mix[2][3] = .25; // Rear Left to LFE
                        speaker_mix[3][3] = .25; // Rear Right to LFE
                        speaker_mix[2][4] = 1; // Rear Left to Rear Left
                        speaker_mix[3][5] = 1; // Rear Right to Rear Right
                        speaker_mix[0][6] = .5; // Front Left to Side Left
                        speaker_mix[1][7] = .5; // Front Right to Side Right
                        speaker_mix[2][6] = .5; // Rear Left to Side Left
                        speaker_mix[3][7] = .5; // Rear Right to Side Right
                        break;
                }
            }
            break;
        case 6:
            switch (output_channels) {
                case 1: // 5.1 Surround -> Mono
                    speaker_mix[0][0] = .2; // Front Left to Mono
                    speaker_mix[1][0] = .2; // Front Right to Mono
                    speaker_mix[2][0] = .2; // Center to Mono
                    speaker_mix[4][0] = .2; // Rear Left to Mono
                    speaker_mix[5][0] = .2; // Rear Right to Mono
                    break;
                case 2: // 5.1 Surround -> Stereo
                    speaker_mix[0][0] = .33; // Front Left to Left
                    speaker_mix[1][1] = .33; // Front Right to Right
                    speaker_mix[2][0] = .33; // Center to Left
                    speaker_mix[2][1] = .33; // Center to Right
                    speaker_mix[4][0] = .33; // Rear Left to Left
                    speaker_mix[5][1] = .33; // Rear Right to Right
                    break;
                case 4: 
                    if (output_chlayout1 == 0x33 && output_chlayout2 == 0x00) { // 5.1 Surround -> Quad
                        speaker_mix[0][0] = .66; // Front Left to Front Left
                        speaker_mix[1][1] = .66; // Front Right to Front Right
                        speaker_mix[2][0] = .33; // Center to Front Left
                        speaker_mix[2][1] = .33; // Center to Front Right
                        speaker_mix[4][2] = 1; // Rear Left to Rear Left
                        speaker_mix[5][3] = 1; // Rear Right to Rear Right
                    }
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
                    speaker_mix[0][0] = 1; // Front Left to Front Left
                    speaker_mix[1][1] = 1; // Front Right to Front Right
                    speaker_mix[2][2] = 1; // Center to Center
                    speaker_mix[3][3] = 1; // LFE to LFE
                    speaker_mix[4][4] = 1; // Rear Left to Rear Left
                    speaker_mix[5][5] = 1; // Rear Right to Rear Right
                    speaker_mix[0][6] = .5; // Front Left to Side Left
                    speaker_mix[1][7] = .5; // Front Right to Side Right
                    speaker_mix[4][6] = .5; // Rear Left to Side Left
                    speaker_mix[5][7] = .5; // Rear Right to Side Right
                    break;
            }
            break;
        case 8:
            switch (output_channels) {
                case 1: // 7.1 Surround -> Mono
                    speaker_mix[0][0] = 1.0f/7.0f; // Front Left to Mono
                    speaker_mix[1][0] = 1.0f/7.0f; // Front Right to Mono
                    speaker_mix[2][0] = 1.0f/7.0f; // Center to Mono
                    speaker_mix[4][0] = 1.0f/7.0f; // Rear Left to Mono
                    speaker_mix[5][0] = 1.0f/7.0f; // Rear Right to Mono
                    speaker_mix[6][0] = 1.0f/7.0f; // Side Left to Mono
                    speaker_mix[7][0] = 1.0f/7.0f; // Side Right to Mono
                    break;
                case 2: // 7.1 Surround -> Stereo
                    speaker_mix[0][0] = 1.0f/7.0f; // Front Left to Mono
                    speaker_mix[1][0] = 1.0f/7.0f; // Front Right to Mono
                    speaker_mix[2][0] = 1.0f/7.0f; // Center to Mono
                    speaker_mix[4][0] = 1.0f/7.0f; // Rear Left to Mono
                    speaker_mix[5][0] = 1.0f/7.0f; // Rear Right to Mono
                    speaker_mix[6][0] = 1.0f/7.0f; // Side Left to Mono
                    speaker_mix[7][0] = 1.0f/7.0f; // Side Right to Mono
                    break;
                case 4: 
                    if (output_chlayout1 == 0x33 && output_chlayout2 == 0x00) { // 7.1 Surround -> Quad
                        speaker_mix[0][0] = .5; // Front Left to Front Left
                        speaker_mix[1][1] = .5; // Front Right to Front Right
                        speaker_mix[2][0] = .25; // Center to Front Left
                        speaker_mix[2][1] = .25; // Center to Front Right
                        speaker_mix[4][2] = .66; // Rear Left to Rear Left
                        speaker_mix[5][3] = .66; // Rear Right to Rear Right
                        speaker_mix[6][0] = .25; // Side Left to Front Left
                        speaker_mix[7][1] = .25; // Side Left to Front Right
                        speaker_mix[6][2] = .33; // Side Left to Rear Left
                        speaker_mix[7][3] = .33; // Side Left to Rear Right
                    }
                    break;
                case 6: // 7.1 Surround -> 5.1 Surround
                    // FL FR C LFE BL BR
                    speaker_mix[0][0] = .66; // Front Left to Front Left
                    speaker_mix[1][1] = .66; // Front Right to Front Right
                    speaker_mix[2][2] = 1; // Center to Center
                    speaker_mix[3][3] = 1; // LFE to LFE
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
    log("Speaker config: " + to_string(input_channels) + " -> " + to_string(output_channels));
}

void check_header() {
    if (memcmp(input_header, packet_in_buffer, HEADER_SIZE) != 0) {
        log("Got new header");
        memcpy(input_header, packet_in_buffer, HEADER_SIZE);
        input_samplerate = (input_header[0] & 0x7F) * ((input_header[0] & 0x80)?44100:48000);
        input_bitdepth = input_header[1];
        input_channels = input_header[2];
        input_chlayout1 = input_header[3];
        input_chlayout2 = input_header[4];
        log("Sample Rate: " + to_string(input_samplerate));
        log("Bit Depth: " + to_string(input_bitdepth));
        log("Channels: " + to_string(input_channels));
        build_speaker_mix();
    }
}

bool handle_receive_buffer() {  // Receive data from input fd
    int bytes_in = read(fd_in, packet_in_buffer, PACKET_SIZE);
    if (bytes_in != PACKET_SIZE) {
        //log("Warn: Got bad input size " + to_string(bytes_in) + " fd: " + to_string(fd_in));
        return false;
    }
    memcpy(receive_buffer,packet_in_buffer + HEADER_SIZE, CHUNK_SIZE);
    return true;
}

void scale_buffer() {
    if (input_bitdepth != 16 && input_bitdepth != 24 && input_bitdepth != 32)
        return;
    for (int i = 0; i < CHUNK_SIZE; i+=input_bitdepth / 8) {
        switch(input_bitdepth) {
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
        scale_buffer_uint8_pos += 4;
    }
}

void volume_buffer() {
    for (int i=0;i<scale_buffer_uint8_pos / sizeof(uint32_t); i++)
        scaled_buffer[i] = scaled_buffer[i] * volume;
}

void break_buffer_to_channels() {
    for (int i=0;i<scale_buffer_uint8_pos / sizeof(uint32_t); i++) {
        int channel = i % input_channels;
        int pos = i / input_channels;
        channel_buffers[channel][pos] = scaled_buffer[i];
    }
    channel_buffer_pos = scale_buffer_uint8_pos / sizeof(uint32_t) / input_channels;
}

void merge_channels_to_buffer() {
    for (int channel=0; channel < output_channels; channel++)
        for (int pos=0; pos < channel_buffer_pos; pos++) {
            int eq_buffer_pos = sizeof(post_eq_buffer[channel]) / sizeof(uint32_t) - channel_buffer_pos + pos;
            scaled_buffer[pos * output_channels + channel] = remixed_channel_buffers[channel][pos];
        }
    scale_buffer_uint8_pos = channel_buffer_pos * sizeof(uint32_t) * output_channels;
}

void mix_speakers() {
    memset(remixed_channel_buffers, 0, sizeof(remixed_channel_buffers));
    for (int pos=0;pos < channel_buffer_pos; pos++)
        for (int input_channel=0;input_channel<input_channels;input_channel++)
            for (int output_channel=0;output_channel<output_channels;output_channel++)
                remixed_channel_buffers[output_channel][pos] += channel_buffers[input_channel][pos] * speaker_mix[input_channel][output_channel];
}

void resample() {

}

void rotate_eq_buffer() {

}

void equalize() {

}

void write_output_buffer() {
    write(fd_out, scaled_buffer_int8, CHUNK_SIZE);
    memcpy(scaled_buffer_int8, scaled_buffer_int8 + CHUNK_SIZE, sizeof(scaled_buffer) - CHUNK_SIZE);
    scale_buffer_uint8_pos -= CHUNK_SIZE;
}

int main(int argc, char* argv[]) {
    log("Starting");
    process_base_args(argc, argv);
    while (running) {
        if (!handle_receive_buffer())
            continue;
        check_header();
        scale_buffer();
        volume_buffer();
        break_buffer_to_channels();
        mix_speakers();
        rotate_eq_buffer();
        //equalize();
        merge_channels_to_buffer();
        while (scale_buffer_uint8_pos >= CHUNK_SIZE) {     
            write_output_buffer();
        }
    }
    return 0;
}
