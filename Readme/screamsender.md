# ScreamSender

ScreamSender is a Linux utility designed to encapsulate PCM audio streams and send them to a remote Scream receiver or [ScreamRouter](http://github.com/netham45/ScreamRouter) over UDP. While originally intended for LG WebOS TVs, it can run on any Linux-based system.

## Key Features

- Accepts PCM audio input from stdin
- Sends audio streams to specified Scream receivers
- Supports stereo channel layout
- Configurable sample rate and bit depth

## Usage

`screamsender -i -p [-s ] [-b ]`

### Parameters

- `-i <IP Address>`: IP Address of the Scream receiver (Mandatory)
- `-p <Port>`: Port of the Scream receiver (Mandatory)
- `-s <Sample Rate>`: Sample Rate (Default: 48000)
- `-b <Bit Depth>`: Bit Depth (Default: 32)

## Building

### Local Build

Tested on Debian 12 and Alma 9 containers:

1. Install required packages: `g++` and `gcc`
2. Compile with: `g++ screamsender.cpp -o screamsender`

### Cross-compile for aarch64

Tested on a Debian 12 container:

1. Install required packages: `g++-aarch64-linux-gnu`, `gcc-aarch64-linux-gnu`, `binutils-aarch64-linux-gnu`
2. Compile with: `aarch64-linux-gnu-g++ --static screamsender.cpp -o screamsender_aarch64`

## LG WebOS TV Audio Capture

Tested on an LG C1 TV:

`arecord -B 0 -D "hw:1,2" -f S32_LE -c 2 -r 48000 - | screamsender -i <IP Address> -p <Port>`


Note: Capturing audio from HDMI requires the audio to be routed through the TV (e.g., TV speakers, wired headphones, or Bluetooth). ARC/eARC output is not supported. Only stereo audio is available from the TV.

## Integration with ScreamRouter

ScreamSender can be used as a source for ScreamRouter, allowing you to incorporate audio from Linux devices or LG WebOS TVs into your ScreamRouter setup.
