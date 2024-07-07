#include <iostream>
#include <vector>
#include "string.h"
#include <istream>
#include <unistd.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
using namespace std;

#define CHUNK_SIZE 1152
#define HEADER_SIZE 5
#define PACKET_SIZE (CHUNK_SIZE + HEADER_SIZE)

bool running = true;

vector<uint32_t*> receive_buffers;
uint32_t* mixing_buffer = new uint32_t[CHUNK_SIZE/sizeof(uint32_t)];
char output_buffer[PACKET_SIZE * 2] = {0};
int output_buffer_pos = 0;

struct sockaddr_in udp_dest_addr = {};
int udp_output_fd = 0;

int tcp_output_fd = 0;
vector<int> input_fds;
vector<bool> active;
string output_ip = "";
int output_port = 0;
int output_bitdepth = 0;
int output_samplerate = 0;
int output_channels = 0;
int output_chlayout1 = 0;
int output_chlayout2 = 0;

// Holds ints to be parsed from command line, NULL = ignored
int* config_argv[] = {NULL, // Process File Name
                      NULL, // output_ip (string)
                      &output_port,
                      &output_bitdepth,
                      &output_samplerate,
                      &tcp_output_fd,
                      &output_channels,
                      &output_chlayout1,
                      &output_chlayout2};
int config_argc = sizeof(config_argv) / sizeof(int*);

void process_base_args(int argc, char* argv[]) { // Processes fixed arguments
    if (argc <= config_argc)
        ::exit(-1);
    output_ip = string(argv[1]);
    for (int argi = 0; argi < config_argc; argi++)
        if (config_argv[argi] == 0)
            continue;
        else
            *(config_argv[argi]) = atoi(argv[argi]);
}

void process_fd_args(int argc, char* argv[]) { // Processes variable number of fds to use as inputs
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
}

void setup_udp() { // Sets up the UDP socket for output
    udp_output_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    udp_dest_addr.sin_family = AF_INET;
    udp_dest_addr.sin_port = htons(output_port);
    inet_pton(AF_INET, output_ip.c_str(), &udp_dest_addr.sin_addr);
}

void setup_buffers() { // Sets up buffers to receive data from input fds
    for (int buf_idx = 0; buf_idx < input_fds.size(); buf_idx++)
        receive_buffers.push_back((uint32_t*)malloc(CHUNK_SIZE));
}

void handle_receive_buffers() {  // Receive data frominput fds
    for (int fd_idx = 0; fd_idx < input_fds.size(); fd_idx++) {
        struct timeval timeout;
        timeout.tv_sec = 0;
        if (!active[fd_idx])
            timeout.tv_usec = 100; // 100uS, just check if it's got data
        else    
            timeout.tv_usec = 35000; // 10ms, wait for a chunk.
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(input_fds[fd_idx], &readfds);
        bool prev_state = active[fd_idx];
        active[fd_idx] = select(input_fds[fd_idx] + 1, &readfds, NULL, NULL, &timeout) > 0;
        if (prev_state != active[fd_idx])
            cout << "Setting " << input_fds[fd_idx] << " Active: " << active[fd_idx] << endl;

        if (active[fd_idx])
            for (int bytes_in = 0; running && bytes_in < CHUNK_SIZE;)
                bytes_in += read(input_fds[fd_idx], receive_buffers[fd_idx] + bytes_in, CHUNK_SIZE - bytes_in);
    }
}

void mix_buffers() { // Mix all received buffers
    for (int input_buf_idx = 0; input_buf_idx < receive_buffers.size(); input_buf_idx++)
        for (int buf_pos = 0; buf_pos < CHUNK_SIZE / sizeof(uint32_t); buf_pos++)
            if (mixing_buffer[buf_pos] + receive_buffers[input_buf_idx][buf_pos] > UINT32_MAX)
                mixing_buffer[buf_pos] = UINT32_MAX;
            else if (input_buf_idx == 0)
                mixing_buffer[buf_pos] = receive_buffers[input_buf_idx][buf_pos];
            else
                mixing_buffer[buf_pos] += receive_buffers[input_buf_idx][buf_pos];
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
    if (tcp_output_fd && !send(tcp_output_fd, output_buffer + HEADER_SIZE, CHUNK_SIZE, 0) > 0)
        tcp_output_fd = 0;
    else
        sendto(udp_output_fd, output_buffer, PACKET_SIZE, 0, (struct sockaddr *)&udp_dest_addr, sizeof(udp_dest_addr));
}

void rotate_buffer() { // Shifts the last CHUNK_SIZE bytes in output_buffer up to the top
    memcpy(output_buffer + HEADER_SIZE, output_buffer + PACKET_SIZE, CHUNK_SIZE);
    output_buffer_pos -= CHUNK_SIZE;
}

int main(int argc, char* argv[]) {
    process_base_args(argc, argv);
    process_fd_args(argc, argv);
    setup_header();
    setup_udp();
    setup_buffers();

    while (running) {
        handle_receive_buffers();
        mix_buffers();
        downscale_buffer();
        if (output_buffer_pos < CHUNK_SIZE)
          continue;
        send_buffer();
        rotate_buffer();
    }
    return 0;
}
