# Setting up Scream Unix Receiver on Raspberry Pi

This guide will walk you through the process of setting up the Scream Unix receiver on a Raspberry Pi and other Linux systems and configuring it to run on boot.

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

To ensure that the user service starts on boot, even if the user is not logged in, you need to enable linger for your user:

```
sudo loginctl enable-linger $USER
```

## Verifying the Setup

1. Check the status of the Scream service:

   ```
   systemctl --user status scream.service
   ```

2. If everything is set up correctly, you should see that the service is active and running.

## Troubleshooting

- If you encounter any issues, check the system logs:

  ```
  journalctl --user -u scream.service
  ```

- Ensure that your Raspberry Pi's audio output is properly configured.
- Verify that your network settings allow the Scream traffic to reach your Raspberry Pi.