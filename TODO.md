# To-Do

## Phase 1 – ALSA Discovery & Notification Backbone
- Implement Linux-only alsa_device_enumerator to catalog cards/devices as tags ac:<card>.<device> and ap:<card>.<device> with metadata (friendly name, hw string, capabilities, presence).
- Add ALSA hotplug watcher via snd_ctl_subscribe_events that emits DeviceDiscoveryNotification entries to the shared notification queue; deduplicate repetitive events.
- Replace NewSourceNotification with DeviceDiscoveryNotification in audio_types.h and update all queue typedefs/usages accordingly.
- Extend AudioManager with registry cache plus pending_device_events_, expose pybind list_system_devices() and drain_device_notifications(), and ensure clean thread shutdown.

## Phase 2 – ALSA Capture & Playback Components
- [x] Implement AlsaCaptureReceiver (derives NetworkAudioReceiver) that opens ALSA capture devices by tag, configures hw/sw params, reads frames into TaggedAudioPacket, and handles XRUN recovery.
- [x] Extend ReceiverManager with reference-counted ensure/release helpers for AlsaCaptureReceiver instances, exposing them through AudioManager.
- [x] Implement AlsaPlaybackSender (derives INetworkSender) that opens ALSA playback handles, configures params, writes mixed audio with format conversion, and recovers from underruns.
- [x] Update SinkAudioMixer to create AlsaPlaybackSender when SinkConfig.protocol == "alsa" while preserving existing sender branches.

## Phase 3 – Configuration Manager & Solver Integration
- Add SystemAudioDeviceInfo model and system_capture_devices/system_playback_devices lists to configuration schema; migrate YAML to include empty defaults.
- On startup, merge list_system_devices() results into the stored lists and launch a poller that drains device notifications, updating metadata and websocket broadcasts.
- Adjust AudioEngineConfigApplier to call AudioManager ensure/release helpers when source tags start with ac: and to rely on protocol=="alsa" for sinks; leave routing creation to normal linkages.
- Ensure solver/persistence keep system device entries intact while honoring enabled flags and marking routes inactive when referenced tags are currently absent.

## Phase 4 – Frontend & User Workflow Enhancements
- Extend websocket/API payloads to deliver system_capture_devices/system_playback_devices with metadata and presence flags.
- Update UI to surface discovered ALSA devices, allow creating standard source/sink entries referencing ac:/ap: tags, and display availability/warnings for absent hardware.
- Prevent deletion of system device metadata rows and reuse existing enable/disable toggles for activation; surface alerts when routes reference offline tags.

## Phase 5 – Testing, Documentation, and Rollout
- Add C++ unit tests covering enumerator tag generation, notification emission, and ALSA sender/receiver error handling with mocked ALSA calls.
- Extend Python tests for configuration migration, notification handling, and solver behavior with simulated ALSA devices.
- Create Linux CI integration scenario using snd-aloop to validate discovery, configuration, and ALSA I/O; define manual QA checklist.
- Update README/admin docs with ALSA requirements, tag scheme, workflow, troubleshooting, and note build linkage against libasound on Linux.
