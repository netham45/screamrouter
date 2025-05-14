#ifndef SOURCE_INPUT_PROCESSOR_H
#define SOURCE_INPUT_PROCESSOR_H

#include "audio_component.h"
#include "thread_safe_queue.h"
#include "audio_types.h"
#include "audio_processor.h" // Include the existing AudioProcessor

#include <string>
#include <vector>
#include <deque>
#include <chrono>
#include <memory> // For unique_ptr, shared_ptr
#include <mutex>
#include <condition_variable>
#include <map> // For std::map
// #include "../configuration/audio_engine_config_types.h" // CppSpeakerLayout is now in audio_types.h (included above)

namespace screamrouter {
namespace audio {

// Using aliases for clarity
using InputPacketQueue = utils::ThreadSafeQueue<TaggedAudioPacket>;
using OutputChunkQueue = utils::ThreadSafeQueue<ProcessedAudioChunk>;
using CommandQueue = utils::ThreadSafeQueue<ControlCommand>;

// Define constants based on original code/assumptions
// CHUNK_SIZE is defined as a macro in c_utils/audio_processor.h
const size_t SCREAM_HEADER_SIZE = 5; // Size of the raw scream header
const size_t INPUT_CHUNK_BYTES = 1152; // Expected size of audio_data in TaggedAudioPacket (same as CHUNK_SIZE macro)
const int DEFAULT_INPUT_BITDEPTH = 16; // Assume 16-bit input unless specified
const int DEFAULT_INPUT_CHANNELS = 2;  // Assume stereo input unless specified
const int DEFAULT_INPUT_SAMPLERATE = 48000; // Assume 48kHz input unless specified
// Match the number of samples SinkAudioMixer expects in its mixing buffer (SINK_MIXING_BUFFER_SAMPLES)
// which is 576 for the current 16-bit stereo output target.
const size_t OUTPUT_CHUNK_SAMPLES = 576; // Total interleaved 32-bit samples expected in ProcessedAudioChunk


class SourceInputProcessor : public AudioComponent {
public:
    SourceInputProcessor(
        SourceProcessorConfig config,
        std::shared_ptr<InputPacketQueue> input_queue,
        std::shared_ptr<OutputChunkQueue> output_queue,
        std::shared_ptr<CommandQueue> command_queue
    );

    ~SourceInputProcessor() noexcept override; // Added noexcept

    // --- AudioComponent Interface ---
    void start() override;
    void stop() override;

    // --- Configuration & Control ---
    void set_speaker_layouts_config(const std::map<int, screamrouter::audio::CppSpeakerLayout>& layouts_map); // Changed to audio namespace

    // --- Getters for Configuration Info ---
    const std::string& get_instance_id() const { return config_.instance_id; }
    const std::string& get_source_tag() const; // Implementation in .cpp
    const SourceProcessorConfig& get_config() const { return config_; } // Added getter for full config
    std::shared_ptr<InputPacketQueue> get_input_queue() const { return input_queue_; } // Ensure this exists

    // Plugin Data Injection method is removed from SourceInputProcessor
    // void inject_plugin_packet(...); // Removed

protected:
    // --- AudioComponent Interface ---
    void run() override; // The main thread loop - will now manage input/output threads

    // --- Thread loop functions ---
    void input_loop();
    void output_loop();

private:
    // InputProtocolType m_protocol_type; // Removed
    int m_current_ap_input_channels = 0;
    int m_current_ap_input_samplerate = 0;
    int m_current_ap_input_bitdepth = 0;

    SourceProcessorConfig config_;
    std::shared_ptr<InputPacketQueue> input_queue_;
    std::shared_ptr<OutputChunkQueue> output_queue_;
    std::shared_ptr<CommandQueue> command_queue_;

    // Internal State
    std::unique_ptr<AudioProcessor> audio_processor_;
    std::mutex audio_processor_mutex_; // Protects audio_processor_ and related settings

    // Timeshift buffer and related members are removed
    // std::deque<TaggedAudioPacket> timeshift_buffer_;
    // size_t timeshift_buffer_read_idx_ = 0;
    // std::chrono::steady_clock::time_point timeshift_target_play_time_;
    // std::mutex timeshift_mutex_;
    // std::condition_variable timeshift_condition_;

    // Processing buffer (holds output from AudioProcessor before pushing full chunks)
    std::vector<int32_t> process_buffer_;
    // size_t process_buffer_samples_ = 0; // Tracked by process_buffer_.size()

    // Current settings (can be updated by commands)
    float current_volume_;
    std::vector<float> current_eq_;
    int current_delay_ms_; 
    // current_timeshift_backshift_sec_ is removed as direct controller,
    // but SIP still needs to know its configured timeshift to report to AudioManager for TimeshiftManager.
    // This might be stored in config_ or as a member updated by commands.
    // For now, assume it's read from config_ or a similar member if needed for reporting.
    // The actual timeshifting is done by TimeshiftManager.
    float current_timeshift_backshift_sec_config_; // To store the configured value for reporting

    // --- New Speaker Layouts Map Member Variables ---
    std::map<int, screamrouter::audio::CppSpeakerLayout> current_speaker_layouts_map_; // Changed to audio namespace
    std::mutex speaker_layouts_mutex_; // To protect access to current_speaker_layouts_map_
    // Old members removed:
    // std::vector<std::vector<float>> current_speaker_mix_matrix_;
    // bool current_use_auto_speaker_mix_;

    // Thread management (stop_flag_ is inherited from AudioComponent)
    std::thread input_thread_; // This thread will now run the main processing loop
    // std::thread output_thread_; // output_thread_ is removed

    // Methods
    void process_commands(); // Check command queue and update state (non-blocking)
    // Timeshift-specific methods are removed:
    // bool get_next_input_chunk(std::vector<uint8_t>& chunk_data);
    // void update_timeshift_target_time();
    // bool check_readiness_condition();
    // void cleanup_timeshift_buffer();
    // void handle_new_input_packet(TaggedAudioPacket& packet);

    // Audio processing methods
    // void initialize_audio_processor(); // Removed
    void process_audio_chunk(const std::vector<uint8_t>& input_chunk_data); // Calls audio_processor_->processAudio
    void push_output_chunk_if_ready(); // Pushes a full ProcessedAudioChunk to output_queue_

    /**
     * @brief Checks packet format, reconfigures AudioProcessor if needed, and returns audio payload.
     * @param packet The incoming tagged audio packet.
     * @param out_audio_payload_ptr Pointer to store the start of the 1152-byte audio data.
     * @param out_audio_payload_size Pointer to store the size (should be CHUNK_SIZE).
     * @return true if successful and audio payload is valid, false otherwise (e.g., bad packet size).
     */
    bool check_format_and_reconfigure(
        const TaggedAudioPacket& packet,
        const uint8_t** out_audio_payload_ptr,
        size_t* out_audio_payload_size
    );
};

} // namespace audio
} // namespace screamrouter

#endif // SOURCE_INPUT_PROCESSOR_H
