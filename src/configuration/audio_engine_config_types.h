// src/configuration/audio_engine_config_types.h
#ifndef AUDIO_ENGINE_CONFIG_TYPES_H
#define AUDIO_ENGINE_CONFIG_TYPES_H

#include <string>
#include <vector>
#include <map> 
#include <set> 
#include "audio_constants.h" // Include directly for MAX_CHANNELS, EQ_BANDS
#include "audio_types.h" // For audio::SinkConfig, screamrouter::audio::CppSpeakerLayout

// #include "audio_processor.h" // No longer needed for EQ_BANDS here

namespace screamrouter {
namespace config {

// CppSpeakerLayout is now defined in audio_types.h

struct AppliedSourcePathParams {
    std::string path_id;                 // e.g., "DesktopAudio_to_LivingRoomSpeakers"
    std::string source_tag;              // e.g., "192.168.1.100"
    std::string target_sink_id;          // e.g., "LivingRoomSpeakers"
    
    float volume = 1.0f;
    std::vector<float> eq_values;        // Should be initialized to EQ_BANDS size
    int delay_ms = 0;
    float timeshift_sec = 0.0f;
    
    int target_output_channels;
    int target_output_samplerate;
    
    std::string generated_instance_id; // Filled by C++ applier

    // Constructor to initialize eq_values correctly
    // Assuming audio::EQ_BANDS is accessible here. If not, a default size might be needed
    // or this initialization needs to be done carefully.
    // The spec for audio_types.h (bindings.cpp) shows EQ_BANDS is a constant.
    // EQ_BANDS is likely a global constant from audio_processor.h

    std::map<int, screamrouter::audio::CppSpeakerLayout> speaker_layouts_map; // New member, CppSpeakerLayout is in screamrouter::audio namespace
    // Old members removed:
    // std::vector<std::vector<float>> speaker_mix_matrix;
    // bool use_auto_speaker_mix;

    AppliedSourcePathParams() : volume(1.0f), delay_ms(0), timeshift_sec(0.0f),
                                target_output_channels(2), target_output_samplerate(48000) { 
        eq_values.assign(EQ_BANDS, 1.0f); // Default flat EQ
        // speaker_layouts_map will be default-initialized (empty map)
    } 
};

struct AppliedSinkParams {
    std::string sink_id;
    screamrouter::audio::SinkConfig sink_engine_config; // The C++ struct from audio_types.h
    std::vector<std::string> connected_source_path_ids; // List of path_id from AppliedSourcePathParams
};

struct DesiredEngineState {
    std::vector<AppliedSourcePathParams> source_paths;
    std::vector<AppliedSinkParams> sinks;
};

} // namespace config
} // namespace screamrouter

#endif // AUDIO_ENGINE_CONFIG_TYPES_H
