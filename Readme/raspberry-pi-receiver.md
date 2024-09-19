# Setting up Scream Unix Receiver on Raspberry Pi

This guide explains how to set up a Scream Unix receiver on a Raspberry Pi or other Linux systems. This receiver can be used as an audio output destination for ScreamRouter, allowing you to play audio from ScreamRouter on your Raspberry Pi.

## Prerequisites

- Raspberry Pi running a Debian-based Linux distribution (e.g., Raspberry Pi OS)
- Internet connection
- Basic knowledge of terminal commands

## Installation

1. Update your package list and install the required dependencies:

   ```
   sudo apt-get update
   sudo apt-get install libpulse-dev git g++
   ```

2. Clone the Scream repository:

   ```
   git clone https://github.com/duncanthrax/scream.git
   ```

3. Navigate to the Unix receiver directory:

   ```
   cd scream/Receivers/unix/
   ```

4. Configure the build:

   ```
   cmake .
   ```

5. Build the receiver:

   ```
   make
   ```

6. Copy the built executable to a system-wide location:

   ```
   sudo cp scream /usr/bin/scream
   ```

## Setting up Scream as a Systemd User Service

1. Create a systemd user service file:

   ```
   mkdir -p ~/.config/systemd/user/
   nano ~/.config/systemd/user/scream.service
   ```

2. Add the following content to the file:

   ```
   [Unit]
   Description=scream
   After=pulseaudio.service

   [Service]
   ExecStart=/usr/bin/scream -v -u -o pulse -t 60 -l 100

   [Install]
   WantedBy=default.target
   ```

   Note: Adjust the ExecStart line if you need different options for your setup.

3. Save and exit the editor (in nano, press Ctrl+X, then Y, then Enter).

4. Reload the systemd user daemon:

   ```
   systemctl --user daemon-reload
   ```

5. Enable and start the Scream service:

   ```
   systemctl --user enable scream.service
   systemctl --user start scream.service
   ```

## Enabling Linger for Your User

To ensure that the user service starts on boot, even if the user is not logged in, enable linger for your user:

```
sudo loginctl enable-linger $USER
```

## Audio Output Configuration

By default, the Scream receiver uses PulseAudio for audio output. You can change the audio output method by modifying the `-o` option in the ExecStart line of the scream.service file. Available options include:

- `pulse`: PulseAudio (default)
- `alsa`: ALSA
- `jack`: JACK Audio Connection Kit

For example, to use ALSA instead of PulseAudio, change the ExecStart line to:

```
ExecStart=/usr/bin/scream -v -u -o alsa -t 60 -l 100
```

Remember to reload the systemd user daemon and restart the service after making changes.

## Network Configuration

Ensure that your network allows UDP traffic on port 4010 (default for Scream) between ScreamRouter and your Raspberry Pi. You may need to configure your router or firewall to allow this traffic.

## Integration with ScreamRouter

To use this Scream receiver with ScreamRouter:

1. In ScreamRouter, add a new sink with the IP address of your Raspberry Pi.
2. Set the port to 4010 (default for Scream).
3. Configure the audio format settings (bit depth, sample rate, channels) to match your receiver's capabilities.

ScreamRouter will then be able to route audio to your Raspberry Pi using this Scream receiver.

## Troubleshooting

- Check the service status:
  ```
  systemctl --user status scream.service
  ```

- View logs:
  ```
  journalctl --user -u scream.service
  ```

- Ensure that your Raspberry Pi's audio output is properly configured.
- Verify that your network settings allow the Scream traffic to reach your Raspberry Pi.

For more information about ScreamRouter and its features, please refer to the [main README](../README.md) and other documentation files in the Readme directory.