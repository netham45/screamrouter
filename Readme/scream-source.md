# Scream Audio Streaming

Scream is a virtual audio driver and protocol for streaming audio over a network with very low latency.

## Installation on Windows 10 (version 1607 and newer)

### Prerequisites
- To install the Scream driver the system clock must be set before July 7, 2023, which is the certificate's expiration date.

### Configuring the OS
Either

1) Disable Secure Boot in BIOS

Or

2) Set a special registry value to allow cross-signed drivers with Secure Boot enabled:
    ```
    [HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\CI\Policy]
    "UpgradedSystem"=dword:00000001
    ```

* These steps are necessary due to Microsoft's stricter rules for signing kernel drivers on newer Windows 10 installations.

### Configuring Unicast IPv4 IP and Port for Scream Audio Driver

It is recommended to use Unicast to send to ScreamReceiver instead of Multicast.

To set a specific Unicast IPv4 IP address and port for the Scream Audio Driver:

1. Open the Registry Editor by typing `regedit` in the Windows search bar and running it as administrator.

2. Navigate to the following registry key:
   `HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\Scream\Options`

3. Look for the `UnicastIP` value. If it doesn't exist, create a new String Value (REG_SZ) with this name.

4. Double-click on `UnicastIP` and set its value to the desired IPv4 address (e.g., "172.23.1.27").

5. To set the port, look for the `UnicastPort` value. If it doesn't exist, create a new DWORD (32-bit) Value with this name.

6. Double-click on `UnicastPort` and set its value to the desired port number in decimal format (e.g., 16401).

7. Close the Registry Editor and reboot your system for the changes to take effect.

## Background

Scream consists of two main components:

1. A virtual audio driver for Windows that captures audio output 
2. A receiver program that plays the streamed audio on the receiving device. ScreamRouter acts as an intermediary between the driver and receiver to allow mixing, equalization, delays, source copying, etc...

The Scream driver captures raw PCM audio data from Windows and sends it over the network as UDP packets. The receiver listens for these packets and plays them back through the local audio device.

## Scream Protocol

The Scream protocol is very simple, which allows for minimal overhead and very low latency:

- Audio data is sent as raw PCM samples in UDP packets
- A small 5-byte header is prepended to each packet with audio format information.
- Packet payload is 1152 bytes of audio data

The 5-byte header contains:

1. Sampling rate (byte 1)
2. Sample size in bits (byte 2)  
3. Number of channels (byte 3)
4. Channel mask (bytes 4-5) (WAVEFORMATEX)

## Scream Receiver

The Scream receiver program performs the following key functions:

- Listens on the configured UDP port for incoming Scream audio packets
- Parses the 5-byte header to determine audio format
- Buffers incoming audio data to handle network jitter
- Outputs the raw PCM data to the local audio device for playback

The receivers are designed to be very lightweight and have minimal processing overhead to maintain the low latency of the Scream system.