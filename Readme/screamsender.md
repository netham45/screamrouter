# ScreamSender for ScreamRouter

ScreamSender is a Linux utility designed to encapsulate PCM audio streams and send them to ScreamRouter over UDP. This tool allows you to incorporate audio from Linux devices or LG WebOS TVs into your ScreamRouter setup, expanding the range of audio sources available in your network.

## Key Features

- Accepts PCM audio input from stdin
- Sends audio streams to specified Scream receivers or ScreamRouter
- Supports stereo channel layout
- Configurable sample rate and bit depth

## Usage

```
screamsender -i <IP Address> -p <Port> [-s <Sample Rate>] [-b <Bit Depth>]
```

### Parameters

- `-i <IP Address>`: IP Address of ScreamRouter (Mandatory)
- `-p <Port>`: Port of ScreamRouter (Mandatory)
- `-s <Sample Rate>`: Sample Rate (Default: 48000)
- `-b <Bit Depth>`: Bit Depth (Default: 32)

## Building

### Local Build

Tested on Debian 12 and Alma 9 containers:

1. Install required packages: `sudo apt-get install g++ gcc`
2. Compile with: `g++ screamsender.cpp -o screamsender`

### Cross-compile for aarch64

Tested on a Debian 12 container:

1. Install required packages: `sudo apt-get install g++-aarch64-linux-gnu gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu`
2. Compile with: `aarch64-linux-gnu-g++ --static screamsender.cpp -o screamsender_aarch64`

## LG WebOS TV Audio Capture

Tested on an LG C1 TV:

```
arecord -B 0 -D "hw:1,2" -f S32_LE -c 2 -r 48000 - | screamsender -i <ScreamRouter_IP> -p <ScreamRouter_Port>
```

Note: Capturing audio from HDMI requires the audio to be routed through the TV (e.g., TV speakers, wired headphones, or Bluetooth). ARC/eARC output is not supported. Only stereo audio is available from the TV.

## Troubleshooting

- If ScreamRouter is not receiving audio, check that the IP addresses and port numbers match in both ScreamSender and ScreamRouter configurations.
- Ensure that your firewall allows UDP traffic on the specified port.
- For LG WebOS TVs, verify that the correct audio device is selected in the `arecord` command.
- If you experience audio dropouts, try adjusting the buffer size in the `arecord` command (the `-B` parameter).

For more information about ScreamRouter and its features, please refer to the [main README](../README.md) and other documentation files in the Readme directory.
