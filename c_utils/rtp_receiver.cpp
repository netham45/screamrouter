// C++ code for streaming audio data over a network using TCP/UDP protocols.

#include <iostream>
#include <vector>
#include "string.h"
#include <istream>
#include <unistd.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <lame/lame.h>
using namespace std;

// Configuration variables
#define CHUNK_SIZE 1152 // Chunk size in bytes for audio data
#define HEADER_SIZE 5 // Size of the Scream header, which contains information about the output audio stream
#define RTP_HEADER_SIZE 12 // Size of the Scream header, which contains information about the output audio stream
#define PACKET_SIZE (CHUNK_SIZE + HEADER_SIZE) // Total packet size including header and chunk
#define TAG_LENGTH 45
#define DATA_RECEIVE_POS (TAG_LENGTH - (RTP_HEADER_SIZE - HEADER_SIZE))

bool running = true; // Flag to control loop execution

uint8_t buffer[TAG_LENGTH + RTP_HEADER_SIZE + CHUNK_SIZE] = {0}; // Vector of pointers to receive buffers for input streams

uint8_t header[HEADER_SIZE] = {0};

vector<int> output_fds; // Vector of file descriptors for audio input streams

int listen_fd = 0;

// Array to hold integers passed from command line arguments, NULL indicates an argument is ignored
int* config_argv[] = {NULL, // Process File Name
                      &listen_fd};
int config_argc = sizeof(config_argv) / sizeof(int*); // Number of command line arguments to process

void log(const string &message, bool endl = true, bool tag = true) {
    printf("%s %s%s", tag?"[RTP Listener]":"", message.c_str(), endl?"\n":"");
}

// Function to process fixed command line arguments like IP address and output port
void process_args(int argc, char** argv) {
    if (argc <= config_argc)
        ::exit(-1);
    for (int argi = 0; argi < config_argc; argi++)
        if (config_argv[argi] == 0)
            continue;
        else
            *(config_argv[argi]) = atoi(argv[argi]);
}

// Function to process variable number of command line arguments representing file descriptors for input streams
void process_fd_args(int argc, char* argv[]) {
    for (int argi = config_argc; argi < argc; argi++) {
        output_fds.push_back(atoi(argv[argi]));
    }
}

void setup_header() { // Sets up the Scream header
    int output_samplerate = 48000;
    int output_bitdepth = 16;
    int output_channels = 2;
    int output_chlayout1 = 0x03;
    int output_chlayout2 = 0x00;
    bool output_samplerate_44100_base = (output_samplerate % 44100) == 0;
    uint8_t output_samplerate_mult = (output_samplerate_44100_base?44100:48000) / output_samplerate;
    header[0] = output_samplerate_mult + (output_samplerate_44100_base << 7);
    header[1] = output_bitdepth;
    header[2] = output_channels;
    header[3] = output_chlayout1;
    header[4] = output_chlayout2;
    log("Set up Header, Rate: " + to_string(output_samplerate) + ", Bit-Depth" + to_string(output_bitdepth) + ", Channels" + to_string(output_channels));
}
struct sockaddr_in receive_addr;

socklen_t receive_addr_len = sizeof(receive_addr);

bool receive() {
    int bytes = recvfrom(listen_fd, buffer + DATA_RECEIVE_POS, RTP_HEADER_SIZE+CHUNK_SIZE, 0, (struct sockaddr *) &receive_addr, &receive_addr_len);
    if (bytes == -1) 
        ::exit(-1);
    if (bytes != RTP_HEADER_SIZE+CHUNK_SIZE)
        return false;
    return true;
}

void send() {
    memset(buffer, 0, TAG_LENGTH);
    strcpy((char*)buffer, inet_ntoa(receive_addr.sin_addr));
    memcpy(buffer + TAG_LENGTH, header, HEADER_SIZE);
    for (int fd_idx=0;fd_idx<output_fds.size();fd_idx++)
        write(output_fds[fd_idx], buffer, PACKET_SIZE + TAG_LENGTH);
}


int main(int argc, char* argv[]) {
    process_args(argc, argv);

    process_fd_args(argc, argv);
    log("Input FDs: ", false, true);
    for (int fd_idx = 0; fd_idx < output_fds.size(); fd_idx++)
        log(to_string(output_fds[fd_idx]) + " ", false, false);
    log("", true, false);
    setup_header();

    while (running)
        if (receive())
            send();
        else
            sleep(.2);
    return 0;
}