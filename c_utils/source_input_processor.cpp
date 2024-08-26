#include <iostream>
#include <vector>
#include <string>
#include <unistd.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <deque>
#include <chrono>
#include <thread>
#include <atomic>
#include <sstream>
#include <climits>
#include <mutex>
#include <cstring>
#include "audio_processor.h"

using namespace std;

#define CHUNK_SIZE 1152
#define HEADER_SIZE 5
#define PACKET_SIZE (CHUNK_SIZE + HEADER_SIZE)
#define TAG_SIZE 45

uint8_t packet_in_buffer[PACKET_SIZE + TAG_SIZE];
uint8_t receive_buffer[CHUNK_SIZE];
int32_t processed_buffer[CHUNK_SIZE * 8];
uint8_t *processed_buffer_int8 = (uint8_t *)processed_buffer;

int process_buffer_pos = 0;

string input_ip = "";
int fd_in = 0, fd_out = 0, data_fd_in = 0;
int output_channels = 0, output_samplerate = 0, output_chlayout1 = 0, output_chlayout2 = 0;
int delay = 0, timeshift_buffer_dur = 0;
float volume = 1;
std::chrono::steady_clock::time_point timeshift_last_change;
unsigned long timeshift_buffer_pos = 0;
float timeshift_backshift = 0;
std::mutex timeshift_mutex;

const auto TIMESHIFT_NOREMOVE_TIME = std::chrono::minutes(5);

std::deque<std::pair<std::chrono::steady_clock::time_point, std::vector<uint8_t>>> timeshift_buffer;
std::atomic<bool> threads_running(true);

uint8_t input_header[5] = {0};
int input_channels = 0, input_samplerate = 0, input_bitdepth = 0, input_chlayout1 = 0, input_chlayout2 = 0;

unique_ptr<AudioProcessor> audioProcessor = NULL;
std::mutex audioProcessor_mutex;

float new_eq[EQ_BANDS] = {1};

int *int_args[] = {
    NULL, &fd_in, &fd_out, &data_fd_in, &output_channels, &output_samplerate, &output_chlayout1, &output_chlayout2,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    &delay, &timeshift_buffer_dur,
};

float *float_args[] = {
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    &volume,
    &new_eq[0], &new_eq[1], &new_eq[2], &new_eq[3], &new_eq[4], &new_eq[5], &new_eq[6], &new_eq[7], &new_eq[8], &new_eq[9],
    &new_eq[10],&new_eq[11],&new_eq[12],&new_eq[13],&new_eq[14],&new_eq[15],&new_eq[16],&new_eq[17],
    NULL, NULL,
};

int config_argc = sizeof(int_args) / sizeof(int *);

void log(const string& message) {
    cerr << "[Source Input Processor " << getpid() << "] " << message << endl;
}

void process_args(int argc, char *argv[]) {
    if (argc <= config_argc) {
        log("Too few args");
        threads_running = false;
    }
    input_ip = string(argv[1]);
    for (int argi = 0; argi < config_argc; argi++)
        if (int_args[argi] != NULL)
            *(int_args[argi]) = atoi(argv[argi + 1]);
    for (int argi = 0; argi < config_argc; argi++)
        if (float_args[argi] != NULL)
            *(float_args[argi]) = atof(argv[argi + 1]);
}

// This function checks if the incoming packet header has changed and updates the input parameters accordingly.
void check_update_header() {
    // Compare the current header with the new one received in the packet buffer.
    if (memcmp(input_header, packet_in_buffer + TAG_SIZE, HEADER_SIZE) != 0) {
        log("Got new header");
        // Update the input header with the new data.
        memcpy(input_header, packet_in_buffer + TAG_SIZE, HEADER_SIZE);
        
        // Extract sample rate from the header and convert it to integer format.
        input_samplerate = (input_header[0] & 0x7F) * ((input_header[0] & 0x80) ? 44100 : 48000);
        
        // Extract bit depth from the header and convert it to integer format.
        input_bitdepth = input_header[1];
        
        // Extract number of channels from the header and convert it to integer format.
        input_channels = input_header[2];
        
        // Extract channel layout from the header and convert it to integer format.
        input_chlayout1 = input_header[3];
        input_chlayout2 = input_header[4];
        
        log("Sample Rate: " + to_string(input_samplerate) + " -> " + to_string(output_samplerate));
        log("Bit Depth: " + to_string(input_bitdepth) + " -> 32");
        log("Channels: " + to_string(input_channels) + " -> " + to_string(output_channels));
        
        // Lock the audio processor mutex before updating the audio processor settings.
        audioProcessor_mutex.lock();
        // Create a new AudioProcessor instance with updated parameters.
        audioProcessor = make_unique<AudioProcessor>(input_channels, output_channels, input_bitdepth, input_samplerate, output_samplerate, volume);
        audioProcessor->setEqualizer(new_eq);
        // Unlock the audio processor mutex after updating the settings.
        audioProcessor_mutex.unlock();
    }
}

void receive_data_thread() {
    while (threads_running) {
        while (int bytes = read(fd_in, packet_in_buffer, TAG_SIZE + PACKET_SIZE) != TAG_SIZE + PACKET_SIZE ||
                strcmp(input_ip.c_str(), reinterpret_cast<const char*>(packet_in_buffer)) != 0)
            if (bytes == -1)
                threads_running = false;
        check_update_header();
        auto received_time = std::chrono::steady_clock::now();
        std::vector<uint8_t> new_packet(CHUNK_SIZE);
        memcpy(new_packet.data(), packet_in_buffer + TAG_SIZE + HEADER_SIZE, CHUNK_SIZE);
        timeshift_mutex.lock();
        timeshift_buffer.emplace_back(received_time, std::move(new_packet));
        timeshift_mutex.unlock();
    }
}

void receive_data() {
    try {
        timeshift_mutex.lock();
        while (timeshift_buffer.empty() || timeshift_buffer.size() <= timeshift_buffer_pos || 
               timeshift_buffer.at(timeshift_buffer_pos).first + std::chrono::milliseconds(delay) + 
               std::chrono::milliseconds((int)(timeshift_backshift*1000)) > std::chrono::steady_clock::now()) {
            timeshift_mutex.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            timeshift_mutex.lock();
        }
        timeshift_mutex.unlock();
    } catch (std::out_of_range) {
        log("Out of range 1");
        return;
    }
    memcpy(receive_buffer, timeshift_buffer.at(timeshift_buffer_pos++).second.data(), CHUNK_SIZE);
    if (timeshift_buffer.front().first + std::chrono::milliseconds(delay) + 
        std::chrono::milliseconds((int)(timeshift_backshift*1000)) + 
        std::chrono::seconds(timeshift_buffer_dur) < std::chrono::steady_clock::now()) {
        if (timeshift_last_change + TIMESHIFT_NOREMOVE_TIME < std::chrono::steady_clock::now()) {
            timeshift_mutex.lock();
            timeshift_buffer.pop_front();
            timeshift_buffer_pos--;
            timeshift_mutex.unlock();
        }
    }
}

void change_timeshift() {
    if (timeshift_buffer.size() == 0) {
        timeshift_buffer_pos = 0;
        timeshift_backshift = 0;
    } else {
        timeshift_mutex.lock();
        auto desired_time = std::chrono::steady_clock::now() - 
                            std::chrono::milliseconds((int)(timeshift_backshift*1000)) - 
                            std::chrono::milliseconds(delay);
        long closest_buffer_delta = LONG_MAX;
        
        for (long i=0; i<timeshift_buffer.size(); i++) {
            std::chrono::steady_clock::duration cur_delta = timeshift_buffer.at(i).first - desired_time;
            long cur_delta_num = abs(std::chrono::duration_cast<std::chrono::milliseconds>(cur_delta).count());
            if (cur_delta_num < closest_buffer_delta) {
                closest_buffer_delta = cur_delta_num;
                timeshift_buffer_pos = i;
            }
        }
        
        timeshift_backshift = std::chrono::duration_cast<std::chrono::duration<float>>(
            std::chrono::steady_clock::now() - timeshift_buffer.at(timeshift_buffer_pos).first + 
            std::chrono::milliseconds(delay)
        ).count();
        timeshift_mutex.unlock();
    }
}

void data_input_thread() {
    char line[256];

    while (threads_running) {
        if (read(data_fd_in, line, sizeof(line)) > 0) {
            std::string input(line);
            std::istringstream iss(input);
            std::string command;

            while (std::getline(iss, command)) {
                std::istringstream command_stream(command);
                std::string variable;
                float value;

                if (command_stream >> variable >> value) {
                    if (variable[0] == 'b' && variable.length() > 1 && std::isdigit(variable[1])) {
                        int index = std::stoi(variable.substr(1)) - 1;
                        if (index >= 0 && index < EQ_BANDS) {
                            new_eq[index] = value;
                        }
                    } else if (variable == "v") {
                        if (!audioProcessor)
                            continue;
                        volume = value;
                        audioProcessor_mutex.lock();
                        audioProcessor->setVolume(volume);
                        audioProcessor_mutex.unlock();
                    } else if (variable == "t") {
                        timeshift_backshift = value;
                        change_timeshift();
                    } else if (variable == "d") {
                        delay = (int)value;
                        change_timeshift();
                    }
                } else if (command == "a") {
                    if (!audioProcessor)
                        continue;
                    audioProcessor_mutex.lock();
                    audioProcessor->setEqualizer(new_eq);
                    audioProcessor_mutex.unlock();
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

void write_output_buffer() {
    write(fd_out, processed_buffer_int8, CHUNK_SIZE);
    
    for (int pos = 0; pos < sizeof(processed_buffer) - CHUNK_SIZE; pos++)
        processed_buffer_int8[pos] = processed_buffer_int8[pos + CHUNK_SIZE];
    process_buffer_pos -= CHUNK_SIZE / sizeof(int32_t);
}

int main(int argc, char *argv[]) {
    timeshift_last_change = std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration::min());
    process_args(argc, argv);
    log("Starting source input processor " + input_ip);

    std::thread receive_thread(receive_data_thread);
    std::thread data_thread(data_input_thread);

    while (threads_running) {
        receive_data();
        if (audioProcessor) {
            audioProcessor_mutex.lock();
            int processed_samples = audioProcessor->processAudio(receive_buffer, processed_buffer + process_buffer_pos);
            audioProcessor_mutex.unlock();
            process_buffer_pos += processed_samples;

            while (process_buffer_pos >= CHUNK_SIZE / sizeof(int32_t))
                write_output_buffer();
        }
    }

    receive_thread.join();
    data_thread.join();

    return 0;
}

