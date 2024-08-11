# RTP Audio Streaming with ScreamRouter

This guide explains how to set up and use the RTP audio streaming functionality with ScreamRouter and PulseAudio.

## How it Works

1. ScreamRouter acts as an RTP receiver, listening for incoming audio data.
2. PulseAudio is configured to send audio to ScreamRouter over RTP.
3. ScreamRouter processes the received audio and routes it to the appropriate output.
4. ScreamRouter will listen for incoming RTP packets with the correct format on port 40000.

## Configuring PulseAudio

To send audio from PulseAudio to ScreamRouter via RTP:

1. Load the RTP sender module in PulseAudio:

```bash
pactl load-module module-rtp-send format=s16le source=<source_name> destination=<ScreamRouter_IP> port=<ScreamRouter_Port> mtu=1164
```

## Configuring Pipewire

Pipewire also supports RTP streaming. Here's how to set it up:

1. First, ensure you have Pipewire and its tools installed. On most modern Linux distributions, it should be installed by default.

2. Create a new Pipewire config file for RTP streaming:

```bash
mkdir -p ~/.config/pipewire/
nano ~/.config/pipewire/rtp-stream.conf
```

3. Add the following content to the file:

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

4. To start the RTP stream, run:

```bash
pipewire -c ~/.config/pipewire/rtp-stream.conf
```

5. To route audio to this new RTP source, you can use tools like `pw-link` or a graphical tool like Helvum.

For example, to link your default audio output to the RTP stream:

```bash
pw-link "$(pw-dump | jq -r '.[] | select(.info.props."media.class" == "Audio/Sink") | .info.props."node.name"')" RTP_Source
```

This command finds your default audio sink and links it to the RTP source created.
