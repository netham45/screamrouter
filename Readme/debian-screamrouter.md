# Debian ScreamRouter Installation Guide

This guide provides step-by-step instructions for installing ScreamRouter on a Debian-based system. ScreamRouter is a versatile audio routing and management system designed for network audio streaming.

## System Requirements

- Debian-based Linux distribution
- Root or sudo access to your system

Note: While this guide is for a direct installation, Docker is recommended for easier setup and management.

## Prerequisites

Ensure you have root or sudo access to your Debian system.

## Installation Steps

1. Update the package sources and install required dependencies:

   Run the following command to update the package sources and install necessary packages:

   ```
   sudo sed -i 's/ main/ main contrib non-free security/g' /etc/apt/sources.list.d/*  # Ensure contrib/non-free/security are enabled, skip if not needed
   sudo apt-get update -y
   sudo apt-get install -y libmp3lame0 libmp3lame-dev gcc git g++ python3 python3-pip libtool pkg-config cmake
   ```

2. Clone the ScreamRouter repository:

   Navigate to your home directory and clone the ScreamRouter repository with its submodules:

   ```
   cd ~
   git clone --recurse-submodules -j8 https://github.com/netham45/screamrouter.git
   ```

3. Install Python requirements:

   Install the required Python packages using pip:

   ```
   pip3 install -r ~/screamrouter/requirements.txt
   ```

4. Build C utilities:

   Navigate to the c_utils directory and run the build script:

   ```
   cd ~/screamrouter/c_utils
   ./build.sh
   ```

5. Generate a certificate for HTTPS:

   Or provide your own in the cert/ folder

   ```
   openssl req -new -newkey rsa:4096 -days 365 -nodes -x509 \
     -subj "/C=US/ST=State/L=City/O=Organization/CN=example.com" \
     -keyout ~/screamrouter/cert/privkey.pem -out ~/screamrouter/cert/cert.pem
   ```

6. Run ScreamRouter:

   Finally, navigate to the ScreamRouter directory and run the main script:

   ```
   cd ~/screamrouter
   ./screamrouter.py
   ```

## Accessing ScreamRouter

After installation, you can access the ScreamRouter web interface by opening a web browser and navigating to `https://localhost` (or replace `localhost` with your server's IP address if accessing from another device). Make sure to accept the self-signed certificate warning if you generated your own certificate.

For more information on using ScreamRouter and its features, please refer to the [main README](../README.md) and other documentation files in the Readme directory.
