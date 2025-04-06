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
#ifdef __SSE2__
#include <emmintrin.h>
#endif
#include <immintrin.h>
#include "audio_processor.h"
#include "dcaenc/dcaenc.h"

AudioProcessor *lameProcessor = NULL;
using namespace std;

// Configuration variables
#define CHUNK_SIZE 1152 // Chunk size in bytes for audio data
#define HEADER_SIZE 5 // Size of the Scream header, which contains information about the output audio stream
#define PACKET_SIZE (CHUNK_SIZE + HEADER_SIZE) // Total packet size including header and chunk

bool running = true; // Flag to control loop execution

vector<int32_t*> receive_buffers; // Vector of pointers to receive buffers for input streams
int32_t* mixing_buffer = new int32_t[CHUNK_SIZE / sizeof(int32_t)]; // Mixing buffer to mix received audio data
uint8_t *mixing_buffer_uint8 = (uint8_t*)mixing_buffer;
char output_buffer[PACKET_SIZE * 2] = {0}; // Buffer to store mixed audio data before sending over the network
int output_buffer_pos = 0; // Position in output_buffer for storing audio data
uint8_t mp3_buffer[CHUNK_SIZE * 8];
int mp3_buffer_pos = 0;

lame_t lame = lame_init();

struct sockaddr_in udp_dest_addr = {}; // Socket address structure for UDP socket destination
int udp_output_fd = 0; // File descriptor for the UDP socket

struct timeval receive_timeout;
fd_set read_fds;

int tcp_output_fd = 0; // File descriptor for the TCP socket
int mp3_write_fd = 0; // File descriptor to write mp3 to
vector<int> output_fds; // Vector of file descriptors for audio input streams
bool active[1024] = {0}; // Vector to store whether each input stream is active or not
bool output_active = false; // Flag to indicate if there's any data to be sent over the network
string output_ip = ""; // IP address of the UDP socket destination
int output_port = 0; // Port number of the UDP socket destination
int output_bitdepth = 0; // Bit depth of the output audio stream
int output_samplerate = 0; // Sample rate of the output audio stream
int output_channels = 0; // Number of channels in the output audio stream
int output_chlayout1 = 0; // Channel layout part 1 for the output audio stream
int output_chlayout2 = 0; // Channel layout part 2 for the output audio stream
int use_dts = 0; // Is it outputting DTS?
dcaenc_context_s *dca_context; // DTS encoding context

// Array to hold integers passed from command line arguments, NULL indicates an argument is ignored
int* config_argv[] = {NULL, // Process File Name
                      NULL, // output_ip (string)
                      &output_port,
                      &output_bitdepth,
                      &output_samplerate,
                      &output_channels,
                      &output_chlayout1,
                      &output_chlayout2,
                      &tcp_output_fd,
                      &mp3_write_fd};
int config_argc = sizeof(config_argv) / sizeof(int*); // Number of command line arguments to process

inline void log(const string &message) {
    printf("[Sink Output Processor %s:%i] %s\n", output_ip.c_str(), output_port, message.c_str());
}

// Function to process fixed command line arguments like IP address and output port
inline void process_args(char* argv[], int argc) {
    if (argc <= config_argc)
        ::exit(-1);
    // cppcheck-suppress ctuArrayIndex
    output_ip = string(argv[1]);
    for (int argi = 0; argi < config_argc; argi++)
        if (config_argv[argi] == 0)
            continue;
        else
            *(config_argv[argi]) = atoi(argv[argi]);
}

// Function to process variable number of command line arguments representing file descriptors for input streams
inline void process_fd_args(char* argv[], int argc) {
    for (int argi = config_argc; argi < argc; argi++)
        output_fds.push_back(atoi(argv[argi]));
}

inline void setup_header() { // Sets up the Scream header
    bool output_samplerate_44100_base = (output_samplerate % 44100) == 0;
    uint8_t output_samplerate_mult = (output_samplerate_44100_base?44100:48000) / output_samplerate;
    output_buffer[0] = output_samplerate_mult + (output_samplerate_44100_base << 7);
    output_buffer[1] = output_bitdepth;
    output_buffer[2] = output_channels;
    output_buffer[3] = output_chlayout1;
    output_buffer[4] = output_chlayout2;
    log("Set up Header, Rate: " + to_string(output_samplerate) + ", Bit-Depth" + to_string(output_bitdepth) + ", Channels" + to_string(output_channels));
}

inline void setup_udp() { // Sets up the UDP socket for output
    log("UDP Set Up");
    udp_output_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    udp_dest_addr.sin_family = AF_INET;
    udp_dest_addr.sin_port = htons(output_port);
    inet_pton(AF_INET, output_ip.c_str(), &udp_dest_addr.sin_addr);
    int dscp = 63;
    int val = dscp << 2;
    setsockopt(udp_output_fd, IPPROTO_IP, IP_TOS, &val, sizeof(val));
    if (tcp_output_fd > 0) {
        setsockopt(tcp_output_fd, IPPROTO_IP, IP_TOS, &val, sizeof(val));
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
        newsize = 1152 * 8;
        setsockopt(mp3_write_fd, SOL_SOCKET, SO_SNDBUF, &newsize, sizeof(newsize));
        setsockopt (tcp_output_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        log("TCP Set Up");
    }
}

inline void setup_buffers() { // Sets up buffers to receive data from input fds
    log("Buffers Set Up");
    setbuf(stdout, NULL);
    for (int buf_idx = 0; buf_idx < output_fds.size(); buf_idx++)
        receive_buffers.push_back(static_cast<int32_t*>(malloc(CHUNK_SIZE)));
}

inline void setup_lame() {
    lame_set_in_samplerate(lame, output_samplerate);
    lame_set_VBR(lame, vbr_off);
    lame_init_params(lame);
}

bool lame_active = true;
struct timeval lame_timeout;
fd_set lame_fd;

inline void write_lame() {
    FD_ZERO(&lame_fd);
    FD_SET(mp3_write_fd, &lame_fd);
    lame_timeout.tv_sec = 0;
    lame_timeout.tv_usec = lame_active ? 15000 : 100;
    int result = select(mp3_write_fd + 1, NULL, &lame_fd, NULL, &lame_timeout);
    // ScreamRouter will stop reading from the MP3 FD if there's no clients. Don't encode if there's no reader.
    if (result > 0 && FD_ISSET(mp3_write_fd, &lame_fd)) {
        if (!lame_active) {
            lame_active = true;
            log("MP3 Stream Active");
        }
        int32_t processed_buffer[CHUNK_SIZE / sizeof(uint32_t)];
        int processed_samples = lameProcessor->processAudio(reinterpret_cast<const uint8_t*>(mixing_buffer), processed_buffer);

        mp3_buffer_pos = lame_encode_buffer_interleaved_int(lame, processed_buffer, processed_samples / 2, mp3_buffer, CHUNK_SIZE * 8);
        
        if (mp3_buffer_pos > 0)
            write(mp3_write_fd, mp3_buffer, mp3_buffer_pos);
    }
    else {
        if (lame_active) {
            lame_active = false;
            log("MP3 Stream Inactive");
        }
    }
}
inline bool check_fd_active(int fd, bool is_active) {
    receive_timeout.tv_sec = 0;
        if (!is_active)
            receive_timeout.tv_usec = 0; //don't wait if not active
        else    
            receive_timeout.tv_usec = 70000; // 70ms, wait for a chunk.
        FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);
        bool prev_state = is_active;
        if (select(fd + 1, &read_fds, NULL, NULL, &receive_timeout) < 0)
            cout << "Select failure: " << errno << strerror(errno) << endl;
        is_active = FD_ISSET(fd, &read_fds);
        if (prev_state != is_active)
            log("Setting Input FD #" + to_string(fd) + (is_active ?" Active":" Inactive"));
        return is_active;
}

inline bool handle_receive_buffers() {  // Receive data from input fds
    output_active = false;
    for (int fd_idx = 0; fd_idx < output_fds.size(); fd_idx++) {
        if (active[fd_idx] = check_fd_active(output_fds[fd_idx], active[fd_idx])) {
            for (int bytes_in = 0; running && bytes_in < CHUNK_SIZE;)
                bytes_in += read(output_fds[fd_idx], receive_buffers[fd_idx] + bytes_in, CHUNK_SIZE - bytes_in);
            output_active = true;
        }
    }
    return output_active;
}

void mix_buffers() { // Mix all received buffers
#if defined(__AVX2__)
    for (int buf_pos = 0; buf_pos < CHUNK_SIZE / sizeof(int32_t); buf_pos += 8) {
        __m256i mixing = _mm256_setzero_si256();
        for (int input_buf_idx = 0; input_buf_idx < receive_buffers.size(); input_buf_idx++) {
            if (!active[input_buf_idx])
                continue;
            __m256i receive = _mm256_loadu_si256((__m256i*)&receive_buffers[input_buf_idx][buf_pos]);
            mixing = _mm256_add_epi32(mixing, receive);
        }
        _mm256_storeu_si256((__m256i*)&mixing_buffer[buf_pos], mixing);
    }
#elif defined(__SSE2__)
    for (int buf_pos = 0; buf_pos < CHUNK_SIZE / sizeof(int32_t); buf_pos += 4) {
        __m128i mixing = _mm_setzero_si128();
        for (int input_buf_idx = 0; input_buf_idx < receive_buffers.size(); input_buf_idx++) {
            if (!active[input_buf_idx])
                continue;
            __m128i receive = _mm_load_si128((__m128i*)&receive_buffers[input_buf_idx][buf_pos]);
            mixing = _mm_add_epi32(mixing, receive);
        }
        _mm_store_si128((__m128i*)&mixing_buffer[buf_pos], mixing);
    }
#else
    for (int buf_pos = 0; buf_pos < CHUNK_SIZE / sizeof(int32_t); buf_pos++) {
        mixing_buffer[buf_pos] = 0;
        for (int input_buf_idx = 0; input_buf_idx < receive_buffers.size(); input_buf_idx++) {
            if (!active[input_buf_idx])
                continue;
            mixing_buffer[buf_pos] += receive_buffers[input_buf_idx][buf_pos];
        }
        if (mixing_buffer[buf_pos] > INT32_MAX) {
            mixing_buffer[buf_pos] = INT32_MAX;
        } else if (mixing_buffer[buf_pos] < INT32_MIN) {
            mixing_buffer[buf_pos] = INT32_MIN;
        }
    }
#endif
}

inline void downscale_buffer() { // Copies 32-bit mixing_buffer to <output_bitdepth>-bit output_buffer
    int output_bytedepth = output_bitdepth / 8;
    for (int input_pos = 0;input_pos < CHUNK_SIZE; input_pos++) {
        if (output_buffer_pos % output_bytedepth == 0)
            input_pos += sizeof(uint32_t) - output_bytedepth;
        output_buffer[HEADER_SIZE + output_buffer_pos++] = mixing_buffer_uint8[input_pos];
    }
}

inline void send_buffer() { // Sends a buffer over TCP or UDP depending on which is active
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

inline void rotate_buffer() { // Shifts the last CHUNK_SIZE bytes in output_buffer up to the top
    if (output_buffer_pos >= CHUNK_SIZE) {
        memcpy(output_buffer + HEADER_SIZE, output_buffer + PACKET_SIZE, CHUNK_SIZE);
        output_buffer_pos -= CHUNK_SIZE;
    }
}

inline void dts_encode() {
    if (use_dts == 1) {

    }
}

#include <exception>
#include <stdexcept>
#include <cxxabi.h>
#include <execinfo.h>

void print_stacktrace(int skip = 1) {
    void *callstack[128];
    int frames = backtrace(callstack, 128);
    char **strs = backtrace_symbols(callstack, frames);
    
    for (int i = skip; i < frames; ++i) {
        int status;
        char *demangled = abi::__cxa_demangle(strs[i], NULL, 0, &status);
        if (status == 0) {
            std::cerr << demangled << std::endl;
            free(demangled);
        } else {
            std::cerr << strs[i] << std::endl;
        }
    }
    free(strs);
}

int main(int argc, char* argv[]) {
    try {
    process_args(argv, argc);
    if (use_dts == 1)
    {
        if (output_channels != 6) {
            log("DTS requires 6 channels (5.1), but only " + to_string(output_channels) + " were specified. Exiting.");
            exit(1);
        }
        if (output_samplerate != 44100 && output_samplerate != 48000) {
            log("DTS requires 44.1kHz or 48kHz but  " + to_string(output_channels) + " was specified. Exiting.");
            exit(1);
        }
        dca_context = dcaenc_create(
                        output_samplerate,
                        DCAENC_CHANNELS_3FRONT_2REAR,
                        1509000, // DVD bitrate
                        DCAENC_FLAG_IEC_WRAP | DCAENC_FLAG_LFE | DCAENC_FLAG_28BIT | DCAENC_FLAG_PERFECT_QMF);
    }
    lameProcessor = new AudioProcessor(output_channels, 2, 32, output_samplerate, output_samplerate, 1);
    log("Starting Ouput Mixer, sending UDP to " + output_ip +  ":" + to_string(output_port) + ", TCP Enabled: " + (tcp_output_fd > 0?"Yes":"No"));
    process_fd_args(argv, argc);
    log("Input FDs: ");
    for (int fd_idx = 0; fd_idx < output_fds.size(); fd_idx++)
        log(to_string(output_fds[fd_idx]));
    setup_header();
    setup_lame();
    setup_udp();
    setup_buffers();

    while (running) {
        if (!handle_receive_buffers()) {
            sleep(.5);
            continue;
        }
        mix_buffers();
        write_lame();
        downscale_buffer();
        if (output_buffer_pos < CHUNK_SIZE)
          continue;
        send_buffer();
        rotate_buffer();
    }
    } catch (const std::exception& e) {
        std::cerr << "Caught exception: " << e.what() << std::endl;
        std::cerr << "Stack trace:" << std::endl;
        print_stacktrace();
    } catch (...) {
        std::cerr << "Caught unknown exception" << std::endl;
        std::cerr << "Stack trace:" << std::endl;
        print_stacktrace();
}
    return 0;
}
