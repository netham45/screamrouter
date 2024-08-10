# Debian ScreamRouter Installation Guid

This guide provides step-by-step instructions for installing ScreamRouter on a Debian-based system.

* Docker is recommended.

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

4. Build libsamplerate:

   Navigate to the libsamplerate directory and build it using cmake:

   ```
   cd ~/screamrouter/c_utils/libsamplerate
   cmake .
   make
   ```

5. Create symbolic links:

   Create necessary symbolic links for the libmp3lame:

   ```
   ln -s /usr/lib/x86_64-linux-gnu/libmp3lame.so.0 /usr/lib64/libmp3lame.so

   ```

6. Build C utilities:

   Navigate to the c_utils directory and run the build script:

   ```
   cd ~/screamrouter/c_utils
   ./build.sh
   ```

7. Run ScreamRouter:

   Finally, navigate to the ScreamRouter directory and run the main script:

   ```
   cd /screamrouter
   ./screamrouter.py
   ```
