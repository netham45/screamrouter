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
#include <condition_variable>
#include <cstring>
#include <pthread.h>
#include <sched.h>
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
std::condition_variable timeshift_condition;

const auto TIMESHIFT_NOREMOVE_TIME = std::chrono::minutes(5);

std::deque<std::pair<std::chrono::steady_clock::time_point, std::vector<uint8_t>>> timeshift_buffer;
std::atomic<bool> threads_running(true);

uint8_t input_header[HEADER_SIZE] = {0};
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
        return; // Return early to prevent accessing out-of-bounds array elements
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
    fd_set read_fds;
    struct timeval timeout;
    
    while (threads_running) {
        // Set up select with 5ms timeout
        FD_ZERO(&read_fds);
        FD_SET(fd_in, &read_fds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 5000; // 5ms timeout
        
        // Wait for data with timeout
        int select_result = select(fd_in + 1, &read_fds, NULL, NULL, &timeout);
        
        // If select failed or timed out, continue the loop
        if (select_result <= 0) {
            continue;
        }
        
        // Check if our fd is ready for reading
        if (!FD_ISSET(fd_in, &read_fds)) {
            continue;
        }
        
        int bytes;
        while ((bytes = read(fd_in, packet_in_buffer, TAG_SIZE + PACKET_SIZE)) != TAG_SIZE + PACKET_SIZE ||
                (strcmp(input_ip.c_str(), reinterpret_cast<const char*>(packet_in_buffer)) != 0)) {
                    if (bytes == -1)
                        threads_running = false;
            }
        check_update_header();
        auto received_time = std::chrono::steady_clock::now();
        std::vector<uint8_t> new_packet(CHUNK_SIZE);
        memcpy(new_packet.data(), packet_in_buffer + TAG_SIZE + HEADER_SIZE, CHUNK_SIZE);
        
        // Critical section - add data to buffer
        {
            std::lock_guard<std::mutex> lock(timeshift_mutex);
            timeshift_buffer.emplace_back(received_time, std::move(new_packet));
            // Notify while holding the lock - this is actually the recommended pattern for condition variables
            timeshift_condition.notify_one(); // Notify waiting threads that new data is available
        }
    }
}

bool data_ready() {
    // Check if we have data and haven't reached the end
    if (timeshift_buffer.empty() || timeshift_buffer.size() <= timeshift_buffer_pos) {
        return false;
    }

    // Get current packet's scheduled play time
    auto current_time = timeshift_buffer.at(timeshift_buffer_pos).first + 
                       std::chrono::milliseconds(delay) + 
                       std::chrono::milliseconds((int)(timeshift_backshift*1000));

    // If we're at the last packet, don't allow playback
    if (timeshift_buffer_pos == timeshift_buffer.size() - 1) {
        return false;
    }

    // Otherwise check if it's time to play this packet
    return current_time <= std::chrono::steady_clock::now();
}

bool receive_data() {
    try {
        std::unique_lock<std::mutex> process_lock(timeshift_mutex);
        
        // If no data is ready, wait for notification with a timeout
        if (!data_ready()) {
            timeshift_condition.wait_for(process_lock, std::chrono::seconds(1), [&]() -> bool {
                return data_ready();
            });
        }

        if (!data_ready())
            return false;
        
        // Copy the data while holding the lock
        memcpy(receive_buffer, timeshift_buffer.at(timeshift_buffer_pos++).second.data(), CHUNK_SIZE);

        // Check if we need to clean up old data
        if (!timeshift_buffer.empty() && 
            timeshift_buffer.front().first + std::chrono::milliseconds(delay) + 
            std::chrono::milliseconds((int)(timeshift_backshift*1000)) + 
            std::chrono::seconds(timeshift_buffer_dur) < std::chrono::steady_clock::now()) {
            
            if (timeshift_last_change + TIMESHIFT_NOREMOVE_TIME < std::chrono::steady_clock::now()) {
                timeshift_buffer.pop_front();
                timeshift_buffer_pos--;
            }
        }
        
        return true;
        
    } catch (std::out_of_range) {
        log("Out of range 1");
        return false;
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
    fd_set read_fds;
    struct timeval timeout;

    while (threads_running) {
        // Set up select with 5ms timeout
        FD_ZERO(&read_fds);
        FD_SET(data_fd_in, &read_fds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 5000; // 5ms timeout
        
        // Wait for data with timeout
        int select_result = select(data_fd_in + 1, &read_fds, NULL, NULL, &timeout);
        
        // If select failed or timed out, continue the loop
        if (select_result <= 0) {
            continue;
        }
        
        // Check if our fd is ready for reading
        if (!FD_ISSET(data_fd_in, &read_fds)) {
            continue;
        }
        
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
        }
        
        // No need for sleep here as select already provides the timeout
    }
}

void write_output_buffer() {
    write(fd_out, processed_buffer_int8, CHUNK_SIZE);
    for (int pos = 0; pos < sizeof(processed_buffer) - CHUNK_SIZE; pos++)
        processed_buffer_int8[pos] = processed_buffer_int8[pos + CHUNK_SIZE];
    process_buffer_pos -= CHUNK_SIZE / sizeof(int32_t);
}

#include <exception>
#include <stdexcept>
#include <cxxabi.h>
#include <execinfo.h>

void print_stacktrace() {
    const int MAX_STACK_FRAMES = 100;
    void* stack_traces[MAX_STACK_FRAMES];
    int trace_size = backtrace(stack_traces, MAX_STACK_FRAMES);
    char** stack_strings = backtrace_symbols(stack_traces, trace_size);

    std::cerr << "Stack trace:" << std::endl;
    for (int i = 0; i < trace_size; ++i) {
        std::string stack_string(stack_strings[i]);
        size_t pos = stack_string.find('(');
        size_t pos2 = stack_string.find('+', pos);
        if (pos != std::string::npos && pos2 != std::string::npos) {
            std::string mangled_name = stack_string.substr(pos + 1, pos2 - pos - 1);
            int status;
            char* demangled_name = abi::__cxa_demangle(mangled_name.c_str(), nullptr, nullptr, &status);
            if (status == 0) {
                std::cerr << "  " << i << ": " << demangled_name << std::endl;
                free(demangled_name);
            } else {
                std::cerr << "  " << i << ": " << stack_strings[i] << std::endl;
            }
        } else {
            std::cerr << "  " << i << ": " << stack_strings[i] << std::endl;
        }
    }
    free(stack_strings);
}

void monitor_buffer_levels() {
        while (threads_running) {
        
        // Check process buffer
        size_t process_buffer_size = process_buffer_pos * sizeof(int32_t);
        
        // Calculate percentages
        double process_buffer_percentage = (double)process_buffer_size / (CHUNK_SIZE * 8) * 100;
        
        // Log if any buffer exceeds thresholds
        if (process_buffer_percentage > 100) {
            log("CRITICAL: Buffer overflow - Process: " + std::to_string(process_buffer_percentage) + "%");
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int main(int argc, char *argv[]) {
    try {
        // Pin to CPU core 1
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(1, &cpuset);
        pthread_t current_thread = pthread_self();
        if (pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset) != 0) {
            log("Failed to set CPU affinity to core 1");
        } else {
            log("Successfully pinned to CPU core 1");
        }

        timeshift_last_change = std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration::min());
        process_args(argc, argv);
        log("Starting source input processor " + input_ip);

        std::thread receive_thread(receive_data_thread);
        std::thread data_thread(data_input_thread);
        std::thread monitor_thread(monitor_buffer_levels);

        while (threads_running) {
            if (!receive_data()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
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
        monitor_thread.join();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Exception caught: " << e.what() << std::endl;
        print_stacktrace();
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception caught" << std::endl;
        print_stacktrace();
        return 1;
    }
}
