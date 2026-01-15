import sys
import time
from pathlib import Path

import pytest

ROOT_DIR = Path(__file__).resolve().parents[2]
if str(ROOT_DIR) not in sys.path:
    sys.path.insert(0, str(ROOT_DIR))

import screamrouter_audio_engine as ae

MATRIX_DIM = 8


def _make_sink_params(sink_id: str,
                      path_id: str,
                      protocol: str,
                      samplerate: int,
                      bitdepth: int,
                      channels: int,
                      time_sync_enabled: bool,
                      time_sync_delay_ms: int,
                      output_port: int) -> ae.AppliedSinkParams:
    params = ae.AppliedSinkParams()
    params.sink_id = sink_id
    cfg = params.sink_engine_config
    cfg.id = sink_id
    cfg.friendly_name = f"PyTest {sink_id}"
    cfg.output_ip = "127.0.0.1"
    cfg.output_port = output_port
    cfg.protocol = protocol
    cfg.samplerate = samplerate
    cfg.bitdepth = bitdepth
    cfg.channels = channels
    cfg.time_sync_enabled = time_sync_enabled
    cfg.time_sync_delay_ms = time_sync_delay_ms
    cfg.chlayout1 = 0x03
    cfg.chlayout2 = 0x00
    params.connected_source_path_ids = [path_id]
    return params


def _make_source_path(path_id: str,
                      sink_id: str,
                      source_tag: str,
                      volume: float,
                      eq_normalization: bool,
                      volume_normalization: bool,
                      delay_ms: int,
                      timeshift_sec: float,
                      channels: int,
                      samplerate: int,
                      bitdepth: int,
                      eq_peak_index: int,
                      auto_layout: bool) -> ae.AppliedSourcePathParams:
    path = ae.AppliedSourcePathParams()
    path.path_id = path_id
    path.source_tag = source_tag
    path.target_sink_id = sink_id
    path.volume = volume
    path.eq_normalization = eq_normalization
    path.volume_normalization = volume_normalization
    path.delay_ms = delay_ms
    path.timeshift_sec = timeshift_sec
    path.target_output_channels = channels
    path.target_output_samplerate = samplerate
    path.source_input_channels = channels
    path.source_input_samplerate = samplerate
    path.source_input_bitdepth = bitdepth
    path.eq_values = [0.9] * ae.EQ_BANDS
    path.eq_values[eq_peak_index % ae.EQ_BANDS] = 1.1

    layout = ae.CppSpeakerLayout()
    layout.auto_mode = auto_layout
    layout.matrix = [[0.0 for _ in range(MATRIX_DIM)] for _ in range(MATRIX_DIM)]
    for idx in range(min(channels, MATRIX_DIM)):
        layout.matrix[idx][idx] = 0.75 if not auto_layout else 1.0
    path.speaker_layouts_map = {channels: layout}
    return path


@pytest.fixture(scope="module")
def audio_manager():
    manager = ae.AudioManager()
    assert manager.initialize(0, 5), "AudioManager failed to initialize"
    yield manager
    manager.shutdown()


@pytest.fixture()
def config_applier(audio_manager):
    applier = ae.AudioEngineConfigApplier(audio_manager)
    yield applier
    # Always return engine to empty desired state
    applier.apply_state(ae.DesiredEngineState())


def test_apply_state_python_rapid_fire(audio_manager: ae.AudioManager, config_applier: ae.AudioEngineConfigApplier):
    protocols = ["scream", "rtp"]
    sample_rates = [44100, 48000]
    bit_depths = [16, 24]
    channel_counts = [1, 2, 4]

    iteration = 0
    for protocol in protocols:
        for samplerate in sample_rates:
            for bitdepth in bit_depths:
                for channels in channel_counts:
                    sink_id = f"py-sink-{iteration}"
                    path_id = f"{sink_id}-path"
                    source_tag = f"py-source-{iteration}"
                    volume = 0.55 if (iteration % 2 == 0) else 0.9
                    delay_ms = 20 + iteration
                    timeshift = 0.1 * (iteration % 3)
                    enable_mp3 = iteration % 2 == 0
                    time_sync = iteration % 3 == 0
                    eq_norm = (iteration % 2) == 0
                    vol_norm = (iteration % 3) == 0

                    desired = ae.DesiredEngineState()
                    sink_params = _make_sink_params(
                        sink_id=sink_id,
                        path_id=path_id,
                        protocol=protocol,
                        samplerate=samplerate,
                        bitdepth=bitdepth,
                        channels=channels,
                        time_sync_enabled=time_sync,
                        time_sync_delay_ms=delay_ms,
                        output_port=16000 + iteration,
                    )
                    path_params = _make_source_path(
                        path_id=path_id,
                        sink_id=sink_id,
                        source_tag=source_tag,
                        volume=volume,
                        eq_normalization=eq_norm,
                        volume_normalization=vol_norm,
                        delay_ms=delay_ms,
                        timeshift_sec=timeshift,
                        channels=channels,
                        samplerate=samplerate,
                        bitdepth=bitdepth,
                        eq_peak_index=iteration,
                        auto_layout=True,
                    )
                    desired.sinks.append(sink_params)
                    desired.source_paths.append(path_params)

                    assert config_applier.apply_state(desired), f"Initial apply_state failed for {sink_id}"

                    # mutate properties to force update
                    sink_params.sink_engine_config.time_sync_delay_ms = delay_ms + 15
                    sink_params.sink_engine_config.output_port += 5
                    path_params.volume = min(1.0, volume + 0.25)
                    path_params.delay_ms = delay_ms + 35
                    path_params.timeshift_sec = timeshift + 0.05
                    path_params.volume_normalization = not vol_norm
                    path_params.eq_normalization = not eq_norm
                    path_params.eq_values[(iteration + 1) % ae.EQ_BANDS] = 0.4
                    path_params.speaker_layouts_map[channels].auto_mode = False

                    assert config_applier.apply_state(desired), f"Update apply_state failed for {sink_id}"

                    assert config_applier.apply_state(ae.DesiredEngineState()), f"Cleanup apply_state failed for {sink_id}"

                    iteration += 1
