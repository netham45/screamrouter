# RTP Audio Streaming with ScreamRouter

This guide explains how to set up and use RTP (Real-time Transport Protocol) audio streaming as a source for ScreamRouter. RTP allows you to send audio over a network, which ScreamRouter can then receive and process.

## How it Works

1. ScreamRouter acts as an RTP receiver, listening for incoming audio data.
2. PulseAudio or Pipewire is configured to send audio to ScreamRouter over RTP.
3. ScreamRouter processes the received audio and routes it to the appropriate output.
4. ScreamRouter listens for incoming RTP packets with the correct format on port 40000 by default.

## Configuring PulseAudio

To send audio from PulseAudio to ScreamRouter via RTP:

1. Load the RTP sender module in PulseAudio:

```bash
pactl load-module module-rtp-send format=s16le source=<source_name> destination=<ScreamRouter_IP> port=<ScreamRouter_Port> mtu=1164
```

Replace `<source_name>` with your audio source, `<ScreamRouter_IP>` with the IP address of your ScreamRouter instance, and `<ScreamRouter_Port>` with the port number (default is 40000).

## Configuring Pipewire

To set up RTP streaming with Pipewire:

1. Create a new Pipewire config file for RTP streaming:

```bash
mkdir -p ~/.config/pipewire/
nano ~/.config/pipewire/rtp-stream.conf
```

2. Add the following content to the file:

```conf
context.modules = [
  {   name = libpipewire-module-rtp-source
      args = {
          source.props = {
              node.name = "rtp-sender"
              media.class = "Audio/Source"
              audio.format = "S16LE"
              audio.rate = 48000
              audio.channels = 2
          }
          stream.props = {
              node.name = "rtp-stream"
              node.description = "RTP Stream"
          }
          local.ip = "0.0.0.0"
          destination.ip = "<ScreamRouter_IP>"
          destination.port = <ScreamRouter_Port>
          sender.mtu = 1164
          sender.payloadType = 127
      }
  }
]
```

Replace `<ScreamRouter_IP>` with the IP address of your ScreamRouter instance and `<ScreamRouter_Port>` with the port number (default is 40000).

3. To start the RTP stream:

```bash
pipewire -c ~/.config/pipewire/rtp-stream.conf
```

4. To route audio to this new RTP source, you can use tools like `pw-link` or a graphical tool like Helvum. For example, to link your default audio output to the RTP stream:

```bash
pw-link "$(pw-dump | jq -r '.[] | select(.info.props."media.class" == "Audio/Sink") | .info.props."node.name"')" RTP_Source
```

## Integration with ScreamRouter

To use the RTP stream in ScreamRouter:

1. In your ScreamRouter configuration, add a new source with the following details:
   - Type: RTP
   - IP: The IP address of the machine sending the RTP stream
   - Port: The port used for RTP streaming (default is 40000)

2. Create a route in ScreamRouter to connect this RTP source to your desired sink.

## Troubleshooting

- If ScreamRouter is not receiving audio, check that the IP addresses and port numbers match in both the sender (PulseAudio/Pipewire) and receiver (ScreamRouter) configurations.
- Ensure that your firewall allows UDP traffic on the specified port.
- For PulseAudio, you can use `pactl list short modules` to verify that the RTP module is loaded.
- For Pipewire, check the output of `pw-cli list-objects` to ensure the RTP source is created.

For more information about ScreamRouter and its features, please refer to the [main README](../README.md) and other documentation files in the Readme directory.
