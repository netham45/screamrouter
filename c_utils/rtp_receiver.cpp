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
#include <algorithm>
#include <string>
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

int listen_fd = 0; // fd for audio input

int data_fd = 0; // fd for writing IPs to

vector<string> known_ips = {};

// Array to hold integers passed from command line arguments, NULL indicates an argument is ignored
int* config_argv[] = {NULL, // Process File Name
                      &listen_fd,
                      &data_fd};
int config_argc = sizeof(config_argv) / sizeof(int*); // Number of command line arguments to process

struct sockaddr_in receive_addr;

socklen_t receive_addr_len = sizeof(receive_addr);

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
    // cppcheck-suppress knownConditionTrueFalse
    uint8_t output_samplerate_mult = (output_samplerate_44100_base?44100:48000) / output_samplerate;
    header[0] = output_samplerate_mult + (output_samplerate_44100_base << 7);
    header[1] = output_bitdepth;
    header[2] = output_channels;
    header[3] = output_chlayout1;
    header[4] = output_chlayout2;
    log("Set up Header, Rate: " + to_string(output_samplerate) + ", Bit-Depth" + to_string(output_bitdepth) + ", Channels" + to_string(output_channels));
}

bool parseHeader() {
    // Parse RTP header (first 12 bytes of buffer)
    /*uint8_t version = (buffer[DATA_RECEIVE_POS] >> 6) & 0x03;
    bool padding = (buffer[DATA_RECEIVE_POS] >> 5) & 0x01;
    bool extension = (buffer[DATA_RECEIVE_POS] >> 4) & 0x01;
    uint8_t csrcCount = buffer[DATA_RECEIVE_POS] & 0x0F;
    bool marker = (buffer[DATA_RECEIVE_POS + 1] >> 7) & 0x01;*/
    uint8_t payloadType = buffer[DATA_RECEIVE_POS + 1] & 0x7F;
    /*uint16_t sequenceNumber = (buffer[DATA_RECEIVE_POS + 2] << 8) | buffer[DATA_RECEIVE_POS + 3];
    uint32_t timestamp = (buffer[DATA_RECEIVE_POS + 4] << 24) | (buffer[DATA_RECEIVE_POS + 5] << 16) |
                         (buffer[DATA_RECEIVE_POS + 6] << 8) | buffer[DATA_RECEIVE_POS + 7];
    uint32_t ssrc = (buffer[DATA_RECEIVE_POS + 8] << 24) | (buffer[DATA_RECEIVE_POS + 9] << 16) |
                    (buffer[DATA_RECEIVE_POS + 10] << 8) | buffer[DATA_RECEIVE_POS + 11];*/
    return payloadType == 127; // Return true if parsing was successful
}

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
    strcpy(reinterpret_cast<char*>(buffer), inet_ntoa(receive_addr.sin_addr));
    string ip_address = string(reinterpret_cast<char*>(buffer));
    if (find(known_ips.begin(), known_ips.end(), ip_address) == known_ips.end()) {
        known_ips.push_back(ip_address);
        write(data_fd, (ip_address + "\n").c_str(), ip_address.length());
    }
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
            if (parseHeader())
                send();
        else
            sleep(.2);
    return 0;
}
