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
#include <sys/select.h>
#include <sys/time.h>
#include <lame/lame.h>
#include <algorithm>
#include <string>
using namespace std;

// Configuration variables
#define CHUNK_SIZE 1152 // Chunk size in bytes for audio data
#define IP_LENGTH 15
#define PROGRAM_TAG_LENGTH 30
#define HEADER_SIZE 5 // Size of the Scream header, which contains information about the output audio strea
#define PACKET_SIZE (CHUNK_SIZE + PROGRAM_TAG_LENGTH + HEADER_SIZE) // Total packet size including header and chunk

bool running = true; // Flag to control loop execution

uint8_t buffer[IP_LENGTH + PACKET_SIZE] = {0}; // Vector of pointers to receive buffers for input streams
char* tag = reinterpret_cast<char*>(buffer);

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
    printf("%s %s%s", tag?"[Scream Per-Port Listener]":"", message.c_str(), endl?"\n":"");
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
    int bytes = recvfrom(listen_fd, buffer + IP_LENGTH, PACKET_SIZE, 0, (struct sockaddr *) &receive_addr, &receive_addr_len);
    //printf("Packet in Bytes: %i Process: %s\n", bytes, buffer);
    if (bytes == -1) 
        ::exit(-1);
    if (bytes != PACKET_SIZE)
        return false;
    return true;
}

void set_tag() {
    //Format for buffer is
    //XXX.XXX.XXX.XXXTAGTAGTAGTAGTAGTAGTAGTAGTAGTA\0DATADATA...
    //If the IP does not fill the full size empty space is filled with spaces.
    //The buffer has the TAG and data in it already and just needs the IP written
    memset(tag, ' ', IP_LENGTH); // Clear with space
    strcpy(tag, inet_ntoa(receive_addr.sin_addr)); // Write IP to start
    tag[strlen(tag)] = ' '; // Clear null terminator
    tag[IP_LENGTH + PROGRAM_TAG_LENGTH - 1] = 0; // Ensure it's got a null terminator at the end of the tag
}

void check_if_known() {
   bool already_known = false;
    for (int idx=0;idx<known_ip_procs.size();idx++) {
        if (known_ip_procs.at(idx) == tag) {
            already_known = true;
            break;
        }
    }
    if (!already_known) {
        dprintf(data_fd, "%s\n", tag);
        known_ip_procs.push_back(tag);
    } 
}


void send() {

    // Use select to check if sockets are ready for writing
    fd_set write_fds;
    struct timeval timeout;
    
    // Set timeout to 0 seconds, 100 microseconds (non-blocking)
    timeout.tv_sec = 0;
    timeout.tv_usec = 100;
    
    for (int fd_idx = 0; fd_idx < output_fds.size(); fd_idx++) {
        // Initialize the file descriptor set
        FD_ZERO(&write_fds);
        FD_SET(output_fds[fd_idx], &write_fds);
        
        // Check if this socket is ready for writing
        int select_result = select(output_fds[fd_idx] + 1, NULL, &write_fds, NULL, &timeout);
        
        // Write only if select indicates the socket is ready
        if (select_result > 0 && FD_ISSET(output_fds[fd_idx], &write_fds)) {
            write(output_fds[fd_idx], buffer, PACKET_SIZE + IP_LENGTH);
        }
    }
}

int main(int argc, char* argv[]) {
    log("Start");
    process_args(argc, argv);

    process_fd_args(argc, argv);
    log("Input FDs: ", false, true);
    for (int fd_idx = 0; fd_idx < output_fds.size(); fd_idx++)
        log(to_string(output_fds[fd_idx]) + " ", false, false);
    log("", true, false);

    while (running)
        if (receive())
        {
            set_tag();
            check_if_known();
            send();
        } else {
            sleep(.01);
        }
    return 0;
}
