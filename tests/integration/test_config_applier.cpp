#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <functional>
#include <thread>
#include <vector>

#include "managers/audio_manager.h"
#include "configuration/audio_engine_config_applier.h"
#include "configuration/audio_engine_config_types.h"
#include "audio_types.h"

using namespace std::chrono_literals;
using screamrouter::audio::AudioManager;
using screamrouter::config::AppliedSinkParams;
using screamrouter::config::AppliedSourcePathParams;
using screamrouter::config::DesiredEngineState;

namespace {

class AudioEngineConfigApplierTest : public ::testing::Test {
protected:
    void SetUp() override {
        manager_ = std::make_shared<AudioManager>();
        ASSERT_TRUE(manager_->initialize(0, 5)) << "AudioManager failed to initialize";
        applier_ = std::make_unique<screamrouter::config::AudioEngineConfigApplier>(*manager_);
    }

    void TearDown() override {
        if (manager_) {
            manager_->shutdown();
            manager_.reset();
        }
    }

    AppliedSinkParams MakeSinkParams(const std::string& sink_id) const {
        AppliedSinkParams params;
        params.sink_id = sink_id;
        auto& cfg = params.sink_engine_config;
        cfg.id = sink_id;
        cfg.friendly_name = "Test " + sink_id;
        cfg.output_ip = "127.0.0.1";
        cfg.output_port = 15000;
        cfg.bitdepth = 16;
        cfg.samplerate = 48000;
        cfg.channels = 2;
        cfg.protocol = "scream";
        cfg.chlayout1 = 0x03;
        cfg.chlayout2 = 0x00;
        cfg.enable_mp3 = false;
        cfg.time_sync_enabled = false;
        return params;
    }

    AppliedSourcePathParams MakeSourcePath(const std::string& path_id,
                                           const std::string& sink_id,
                                           const std::string& source_tag) const {
        AppliedSourcePathParams path;
        path.path_id = path_id;
        path.source_tag = source_tag;
        path.target_sink_id = sink_id;
        path.volume = 1.0f;
        path.target_output_channels = 2;
        path.target_output_samplerate = 48000;
        path.source_input_channels = 2;
        path.source_input_samplerate = 48000;
        path.source_input_bitdepth = 16;
        path.delay_ms = 0;
        path.timeshift_sec = 0.0f;
        path.volume_normalization = false;
        path.eq_normalization = false;
        return path;
    }

    bool WaitForCondition(const std::function<bool()>& predicate,
                          std::chrono::milliseconds timeout = 5s,
                          std::chrono::milliseconds interval = 100ms) const {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (predicate()) {
                return true;
            }
            std::this_thread::sleep_for(interval);
        }
        return predicate();
    }

    bool SinkExists(const std::string& sink_id) const {
        const auto stats = manager_->get_audio_engine_stats();
        return std::any_of(stats.sink_stats.begin(), stats.sink_stats.end(),
                           [&](const screamrouter::audio::SinkStats& stats_entry) {
                               return stats_entry.sink_id == sink_id;
                           });
    }

    std::shared_ptr<AudioManager> manager_;
    std::unique_ptr<screamrouter::config::AudioEngineConfigApplier> applier_;
};

TEST_F(AudioEngineConfigApplierTest, ApplyStateCreatesSinkAndSourcePath) {
    const std::string sink_id = "applier-sink";
    const std::string source_tag = "applier-source";
    DesiredEngineState desired;
    desired.sinks.push_back(MakeSinkParams(sink_id));
    desired.source_paths.push_back(MakeSourcePath("path-1", sink_id, source_tag));

    ASSERT_TRUE(applier_->apply_state(desired));

    EXPECT_TRUE(WaitForCondition([&]() { return SinkExists(sink_id); }))
        << "sink stats never reflected creation of " << sink_id;
}

TEST_F(AudioEngineConfigApplierTest, ApplyStateRemovesSinkAndSourcePath) {
    const std::string sink_id = "remove-sink";
    DesiredEngineState initial;
    initial.sinks.push_back(MakeSinkParams(sink_id));
    initial.source_paths.push_back(MakeSourcePath("path-remove", sink_id, "remove-source"));
    ASSERT_TRUE(applier_->apply_state(initial));
    ASSERT_TRUE(WaitForCondition([&]() { return SinkExists(sink_id); }));

    DesiredEngineState empty_state;
    ASSERT_TRUE(applier_->apply_state(empty_state));

    EXPECT_TRUE(WaitForCondition([&]() { return !SinkExists(sink_id); }))
        << "sink stats still reported " << sink_id << " after removal";
}

TEST_F(AudioEngineConfigApplierTest, ApplyStateRapidFirePropertyCombinations) {
    const std::vector<int> sample_rates = {44100, 48000};
    const std::vector<int> bit_depths = {16, 24};
    const std::vector<int> channel_counts = {1, 2, 4};
    const std::vector<std::string> protocols = {"scream", "rtp"};

    int iteration = 0;
    for (const auto& protocol : protocols) {
        for (int samplerate : sample_rates) {
            for (int bitdepth : bit_depths) {
                for (int channels : channel_counts) {
                    for (int rep = 0; rep < 2; ++rep) {
                        const std::string sink_id = "combo-sink-" + std::to_string(iteration);
                        const std::string path_id = sink_id + "-path";
                        const std::string tag = "combo-source-" + std::to_string(iteration);
                        const float volume = rep == 0 ? 0.55f : 0.95f;
                        const bool eq_norm = (iteration + rep) % 2 == 0;
                        const bool vol_norm = (iteration + rep) % 3 == 0;
                        const int delay_ms = rep == 0 ? 0 : 60 + iteration;
                        const float timeshift = rep == 0 ? 0.0f : 0.2f + (0.05f * (iteration % 3));
                        const bool enable_mp3 = (iteration % 2) == 1;
                        const bool time_sync = (iteration % 2) == 0;

                        AppliedSinkParams sink = MakeSinkParams(sink_id);
                        auto& cfg = sink.sink_engine_config;
                        cfg.protocol = protocol;
                        cfg.samplerate = samplerate;
                        cfg.bitdepth = bitdepth;
                        cfg.channels = channels;
                        cfg.enable_mp3 = enable_mp3;
                        cfg.time_sync_enabled = time_sync;
                        cfg.time_sync_delay_ms = delay_ms;
                        cfg.output_port = 15000 + iteration;
                        sink.connected_source_path_ids = {path_id};

                        AppliedSourcePathParams path = MakeSourcePath(path_id, sink_id, tag);
                        path.volume = volume;
                        path.eq_normalization = eq_norm;
                        path.volume_normalization = vol_norm;
                        path.delay_ms = delay_ms;
                        path.timeshift_sec = timeshift;
                        path.target_output_channels = channels;
                        path.target_output_samplerate = samplerate;
                        path.source_input_channels = channels;
                        path.source_input_samplerate = samplerate;
                        path.source_input_bitdepth = bitdepth;
                        path.eq_values.assign(screamrouter::audio::EQ_BANDS, 0.9f);
                        path.eq_values[(iteration + rep) % screamrouter::audio::EQ_BANDS] = 1.1f;
                        screamrouter::audio::CppSpeakerLayout layout;
                        layout.auto_mode = (rep == 0);
                        layout.matrix.assign(screamrouter::audio::MAX_CHANNELS,
                                             std::vector<float>(screamrouter::audio::MAX_CHANNELS, 0.0f));
                        for (int i = 0; i < std::min(channels, screamrouter::audio::MAX_CHANNELS); ++i) {
                            layout.matrix[i][i] = 0.8f;
                        }
                        path.speaker_layouts_map[channels] = layout;

                        DesiredEngineState desired;
                        desired.sinks.push_back(sink);
                        desired.source_paths.push_back(path);

                        SCOPED_TRACE("iteration=" + std::to_string(iteration) + " rep=" + std::to_string(rep));

                        ASSERT_TRUE(applier_->apply_state(desired));
                        ASSERT_TRUE(WaitForCondition([&]() { return SinkExists(sink_id); }))
                            << "sink stats never reflected creation of " << sink_id;

                        desired.sinks[0].sink_engine_config.enable_mp3 = !enable_mp3;
                        desired.sinks[0].sink_engine_config.time_sync_delay_ms = delay_ms + 25;
                        desired.source_paths[0].volume = std::min(1.0f, volume + 0.25f);
                        desired.source_paths[0].delay_ms = delay_ms + 15;
                        desired.source_paths[0].timeshift_sec = timeshift + 0.05f;
                        desired.source_paths[0].volume_normalization = !vol_norm;
                        desired.source_paths[0].eq_normalization = !eq_norm;
                        desired.source_paths[0].eq_values[(iteration + rep + 1) % screamrouter::audio::EQ_BANDS] = 0.6f;
                        desired.source_paths[0].speaker_layouts_map[channels].auto_mode = false;

                        ASSERT_TRUE(applier_->apply_state(desired));
                        ASSERT_TRUE(WaitForCondition([&]() { return SinkExists(sink_id); }));

                        DesiredEngineState empty_state;
                        ASSERT_TRUE(applier_->apply_state(empty_state));
                        ASSERT_TRUE(WaitForCondition([&]() { return !SinkExists(sink_id); }))
                            << "sink stats still reported " << sink_id << " after removal cycle " << iteration;

                        ++iteration;
                    }
                }
            }
        }
    }
}

}  // namespace
