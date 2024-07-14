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
using namespace std;

// Configuration variables
#define CHUNK_SIZE 1152 // Chunk size in bytes for audio data
#define HEADER_SIZE 5 // Size of the Scream header, which contains information about the output audio stream
#define PACKET_SIZE (CHUNK_SIZE + HEADER_SIZE) // Total packet size including header and chunk

bool running = true; // Flag to control loop execution

vector<uint32_t*> receive_buffers; // Vector of pointers to receive buffers for input streams
uint32_t* mixing_buffer = new uint32_t[CHUNK_SIZE / sizeof(uint32_t)]; // Mixing buffer to mix received audio data
char output_buffer[PACKET_SIZE * 2] = {0}; // Buffer to store mixed audio data before sending over the network
int output_buffer_pos = 0; // Position in output_buffer for storing audio data

struct sockaddr_in udp_dest_addr = {}; // Socket address structure for UDP socket destination
int udp_output_fd = 0; // File descriptor for the UDP socket

struct timeval receive_timeout;
fd_set read_fds;

int tcp_output_fd = 0; // File descriptor for the TCP socket
vector<int> input_fds; // Vector of file descriptors for audio input streams
vector<bool> active; // Vector to store whether each input stream is active or not
bool output_active = false; // Flag to indicate if there's any data to be sent over the network
string output_ip = ""; // IP address of the UDP socket destination
int output_port = 0; // Port number of the UDP socket destination
int output_bitdepth = 0; // Bit depth of the output audio stream
int output_samplerate = 0; // Sample rate of the output audio stream
int output_channels = 0; // Number of channels in the output audio stream
int output_chlayout1 = 0; // Channel layout part 1 for the output audio stream
int output_chlayout2 = 0; // Channel layout part 2 for the output audio stream

// Array to hold integers passed from command line arguments, NULL indicates an argument is ignored
int* config_argv[] = {NULL, // Process File Name
                      NULL, // output_ip (string)
                      &output_port,
                      &output_bitdepth,
                      &output_samplerate,
                      &tcp_output_fd,
                      &output_channels,
                      &output_chlayout1,
                      &output_chlayout2};
int config_argc = sizeof(config_argv) / sizeof(int*); // Number of command line arguments to process

void log(string message) {
    printf("[Sink Output Processor %s:%i] %s\n", output_ip.c_str(), output_port, message.c_str());
}

// Function to process fixed command line arguments like IP address and output port
void process_base_args(int argc, char* argv[]) {
    if (argc <= config_argc)
        ::exit(-1);
    output_ip = string(argv[1]);
    for (int argi = 0; argi < config_argc; argi++)
        if (config_argv[argi] == 0)
            continue;
        else
            *(config_argv[argi]) = atoi(argv[argi]);
}

// Function to process variable number of command line arguments representing file descriptors for input streams
void process_fd_args(int argc, char* argv[]) {
    for (int argi = config_argc; argi < argc; argi++) {
        input_fds.push_back(atoi(argv[argi]));
        active.push_back(false);
    }
}

void setup_header() { // Sets up the Scream header
    bool output_samplerate_44100_base = (output_samplerate % 44100) == 0;
    uint8_t output_samplerate_mult = (output_samplerate_44100_base?44100:48000) / output_samplerate;
    output_buffer[0] = output_samplerate_mult + (output_samplerate_44100_base << 7);
    output_buffer[1] = output_bitdepth;
    output_buffer[2] = output_channels;
    output_buffer[3] = output_chlayout1;
    output_buffer[4] = output_chlayout2;
    log("Set up Header, Rate: " + to_string(output_samplerate) + ", Bit-Depth" + to_string(output_bitdepth) + ", Channels" + to_string(output_channels));
}

void setup_udp() { // Sets up the UDP socket for output
    log("UDP Set Up");
    udp_output_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    udp_dest_addr.sin_family = AF_INET;
    udp_dest_addr.sin_port = htons(output_port);
    inet_pton(AF_INET, output_ip.c_str(), &udp_dest_addr.sin_addr);
    if (tcp_output_fd > 0) {
        fd_set fd;
        timeval tv;
        FD_ZERO(&fd);
        FD_SET(tcp_output_fd, &fd);
        tv.tv_sec = 15;
        tv.tv_usec = 0;
        u_long yes = 1;
        fcntl(tcp_output_fd, F_SETFL, fcntl(tcp_output_fd, F_GETFL, 0) | O_NONBLOCK);
        fcntl(tcp_output_fd, F_SETFL, fcntl(tcp_output_fd, F_GETFL, 0) | O_NDELAY);
        setsockopt(tcp_output_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        int newsize = 1152 * 16;
        setsockopt(tcp_output_fd, SOL_SOCKET, SO_SNDBUF, &newsize, sizeof(newsize));
        setsockopt (tcp_output_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        log("TCP Set Up");

    }
}

void setup_buffers() { // Sets up buffers to receive data from input fds
    log("Buffers Set Up");
    setbuf(stdout, NULL);
    for (int buf_idx = 0; buf_idx < input_fds.size(); buf_idx++)
        receive_buffers.push_back((uint32_t*)malloc(CHUNK_SIZE));
}

bool handle_receive_buffers() {  // Receive data frominput fds
    output_active = false;
    for (int fd_idx = 0; fd_idx < input_fds.size(); fd_idx++) {
        receive_timeout.tv_sec = 0;
        if (!active[fd_idx])
            receive_timeout.tv_usec = 100; // 100uS, just check if it's got data
        else    
            receive_timeout.tv_usec = 70000; // 10ms, wait for a chunk.
        FD_ZERO(&read_fds);
        FD_SET(input_fds[fd_idx], &read_fds);
        bool prev_state = active[fd_idx];
        if (select(input_fds[fd_idx] + 1, &read_fds, NULL, NULL, &receive_timeout) < 0)
            cout << "Select failure: " << errno << strerror(errno) << endl;
        active[fd_idx] = FD_ISSET(input_fds[fd_idx], &read_fds);
        if (prev_state != active[fd_idx])
            log("Setting Input FD #" + to_string(input_fds[fd_idx]) + (active[fd_idx]?" Active":" Inactive"));

        if (active[fd_idx]) {
            for (int bytes_in = 0; running && bytes_in < CHUNK_SIZE;)
                bytes_in += read(input_fds[fd_idx], receive_buffers[fd_idx] + bytes_in, CHUNK_SIZE - bytes_in);
            output_active = true;
        }
    }
    return output_active;
}

void mix_buffers() { // Mix all received buffers
    bool first_buffer = true;
    for (int input_buf_idx = 0; input_buf_idx < receive_buffers.size(); input_buf_idx++) {
        if (!active[input_buf_idx])
            continue;
        for (int buf_pos = 0; buf_pos < CHUNK_SIZE / sizeof(uint32_t); buf_pos++) {
            if (mixing_buffer[buf_pos] + receive_buffers[input_buf_idx][buf_pos] > UINT32_MAX) {
                mixing_buffer[buf_pos] = UINT32_MAX;
            } else if (first_buffer) {
                mixing_buffer[buf_pos] = receive_buffers[input_buf_idx][buf_pos];
            } else {
                mixing_buffer[buf_pos] += receive_buffers[input_buf_idx][buf_pos];
            }
        }
        first_buffer = false;
    }
}

void downscale_buffer() { // Copies 32-bit mixing_buffer to <output_bitdepth>-bit output_buffer
    for (int i = 0; i < CHUNK_SIZE/sizeof(uint32_t); i++) {
        uint8_t* p = (uint8_t*)&mixing_buffer[i];
        if (output_bitdepth >= 32)
            output_buffer[HEADER_SIZE + output_buffer_pos++] = p[0];
        if (output_bitdepth >= 24)
            output_buffer[HEADER_SIZE + output_buffer_pos++] = p[1];
        output_buffer[HEADER_SIZE + output_buffer_pos++] = p[2];
        output_buffer[HEADER_SIZE + output_buffer_pos++] = p[3];
    }
}

void send_buffer() { // Sends a buffer over TCP or UDP depending on which is active
    if (tcp_output_fd) {
        int result = send(tcp_output_fd, output_buffer + HEADER_SIZE, CHUNK_SIZE, 0);
        if (result <= 0) {
            if (errno != EAGAIN) { // Resource Temporary Unavailable (buffer full)
                log("Got TCP error: " + to_string(errno) + ")");
                //close(tcp_output_fd);
                tcp_output_fd = 0;
            }
        }
    } else
        sendto(udp_output_fd, output_buffer, PACKET_SIZE, 0, (struct sockaddr *)&udp_dest_addr, sizeof(udp_dest_addr));
}

void rotate_buffer() { // Shifts the last CHUNK_SIZE bytes in output_buffer up to the top
    if (output_buffer_pos >= CHUNK_SIZE) {
        memcpy(output_buffer + HEADER_SIZE, output_buffer + PACKET_SIZE, CHUNK_SIZE);
        output_buffer_pos -= CHUNK_SIZE;
    }
}

int main(int argc, char* argv[]) {
    process_base_args(argc, argv);

    log("Starting Ouput Mixer, sending UDP to " + output_ip +  ":" + to_string(output_port) + ", TCP Enabled: " + (tcp_output_fd > 0?"Yes":"No"));
    process_fd_args(argc, argv);
    log("Input FDs: ");
    for (int fd_idx = 0; fd_idx < input_fds.size(); fd_idx++)
        log(to_string(input_fds[fd_idx]));
    setup_header();
    setup_udp();
    setup_buffers();

    while (running) {
        if (!handle_receive_buffers()) {
            sleep(.2);
            continue;
        }
        mix_buffers();
        downscale_buffer();
        if (output_buffer_pos < CHUNK_SIZE)
          continue;
        send_buffer();
        rotate_buffer();
    }
    return 0;
}
