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
#include <pthread.h>
#include <sched.h>
using namespace std;

// Configuration variables
#define CHUNK_SIZE 1152 // Chunk size in bytes for audio data
#define HEADER_SIZE 5 // Size of the Scream header, which contains information about the output audio stream
#define PACKET_SIZE (CHUNK_SIZE + HEADER_SIZE) // Total packet size including header and chunk
#define TAG_LENGTH 45

bool running = true; // Flag to control loop execution

uint8_t buffer[TAG_LENGTH + PACKET_SIZE] = {0}; // Vector of pointers to receive buffers for input streams

vector<int> output_fds; // Vector of file descriptors for audio input streams

int listen_fd = 0; // fd for audio input

int data_fd = 0; // fd for writing IPs to

vector<string> known_ip_procs = {};

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

bool receive() {
    int bytes = recvfrom(listen_fd, buffer + TAG_LENGTH, PACKET_SIZE, 0, (struct sockaddr *) &receive_addr, &receive_addr_len);
    if (bytes == -1) 
        ::exit(-1);
    if (bytes != PACKET_SIZE)
        return false;
    return true;
}

void send() {
    memset(buffer, 0, TAG_LENGTH);
    strcpy(reinterpret_cast<char*>(buffer), inet_ntoa(receive_addr.sin_addr));
    string ip_address = string(reinterpret_cast<char*>(buffer));
    if (find(known_ip_procs.begin(), known_ip_procs.end(), ip_address) == known_ip_procs.end()) {
        known_ip_procs.push_back(ip_address);
        write(data_fd, (ip_address + "\n").c_str(), ip_address.length());
    }
    for (int fd_idx=0;fd_idx<output_fds.size();fd_idx++)
        write(output_fds[fd_idx], buffer, PACKET_SIZE + TAG_LENGTH);
}

int main(int argc, char* argv[]) {
    // Pin to CPU core 1
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(1, &cpuset);
    pthread_t current_thread = pthread_self();
    if (pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset) != 0) {
        log("Failed to set CPU affinity to core 1", true, true);
    } else {
        log("Successfully pinned to CPU core 1", true, true);
    }

    process_args(argc, argv);

    process_fd_args(argc, argv);
    log("Input FDs: ", false, true);
    for (int fd_idx = 0; fd_idx < output_fds.size(); fd_idx++)
        log(to_string(output_fds[fd_idx]) + " ", false, false);
    log("", true, false);

    while (running)
        if (receive())
            send();
        else
            sleep(.2);
    return 0;
}
