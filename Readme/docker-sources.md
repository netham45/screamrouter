# Docker Sources for ScreamRouter

ScreamRouter can integrate with various audio streaming services using Docker containers. These containers run audio streaming applications and output audio as RTP streams, which can then be processed by ScreamRouter. This approach allows for easy setup and isolation of different audio sources.

## Available Docker Sources

The following Docker sources are available for use with ScreamRouter:

1. [Amazon Music Docker Container](https://github.com/netham45/screamrouter-amazon-music-docker)
2. [Spotify Docker Container](https://github.com/netham45/screamrouter-spotify-docker)
3. [Firefox Docker Container](https://github.com/netham45/screamrouter-firefox-docker)

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

## Setting Up Docker Sources

To set up a Docker source:

1. Clone the repository for the desired source:
   ```
   git clone https://github.com/netham45/screamrouter-[source-name]-docker.git
   ```

2. Navigate to the cloned directory:
   ```
   cd screamrouter-[source-name]-docker
   ```

3. Build the Docker image:
   ```
   ./build.sh
   ```

4. Run the Docker container:
   ```
   ./run.sh
   ```

5. Access the application interface via VNC (similar to `172.17.0.2:5900`)

6. Configure ScreamRouter to recognize the new source (this should happen automatically in most cases)

## Customization

You can modify the Dockerfiles and configuration files to:

- Change audio settings
- Adjust container resources
- Add or remove features

## Security Considerations

When using these Docker containers, keep in mind:

- They may contain proprietary software or require login credentials for streaming services
- VNC access should be properly secured, especially if exposed to the internet



For more information on using ScreamRouter and its features, please refer to the [main README](../README.md) and other documentation files in the Readme directory.