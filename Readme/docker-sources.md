# Docker Sources for ScreamRouter

There are a few Docker configurations for various audio streaming services that can be used with ScreamRouter. These Docker containers are designed to run audio streaming applications and output audio as RTP streams, which can then be processed by ScreamRouter.

## Common Features

Most Docker sources share these characteristics:

- Based on a lightweight Linux distribution (e.g., Debian)
- Include VNC server for remote GUI access
- Use Wine to run Windows applications when necessary
- Include PulseAudio for audio processing
- Output audio as 16-bit 48kHz PCM encapsulated in RTP streams
- Listen on specific ports for control commands (e.g., play, pause, next track)

## Integration with ScreamRouter

These Docker sources are designed to work seamlessly with ScreamRouter:

- They output RTP streams to the Docker host (usually 172.17.0.1)
- ScreamRouter's auto-detection should recognize the streams
- Audio can be controlled via UDP packets sent to specific ports

## Customization

You can modify the Dockerfiles and configuration files to:

- Change audio settings
- Adjust container resources
- Add or remove features