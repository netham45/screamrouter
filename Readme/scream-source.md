# Scream Audio Streaming for ScreamRouter

This guide explains how to set up Scream as an audio source for ScreamRouter on Windows systems. Scream is a virtual audio driver and protocol for streaming audio over a network with very low latency, which can be used to send audio from Windows to ScreamRouter.

## Desktop Tool (Recommended Method)

The desktop tool is the easiest way to set up Scream streaming on Windows.

1. Download the desktop tool from [here](https://github.com/netham45/windows-scream-sender).
2. Run `install_tool.bat` and allow it to elevate to administrator to create a service under your user.
3. When prompted, enter:
   - IP: Your ScreamRouter IP
   - Port: The port configured in ScreamRouter (default is 4010)
   - Multicast: Choose 'No' for direct connection to ScreamRouter

Administrator access is required to allow the tool to set itself as real-time priority.

## Scream Driver Method

If you prefer to use the Scream driver, follow these steps:

### Driver Installation on Windows 10 (version 1607 and newer)

#### Prerequisites
- The system clock must be set before July 7, 2023 (certificate expiration date).

#### Configuring the OS
Either:
1. Disable Secure Boot in BIOS

Or:
2. Set a registry value to allow cross-signed drivers with Secure Boot enabled:
   ```
   [HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\CI\Policy]
   "UpgradedSystem"=dword:00000001
   ```

### Configuring Unicast IPv4 IP and Port

1. Open Registry Editor as administrator.
2. Navigate to: `HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\Scream\Options`
3. Create or modify these values:
   - `UnicastIP` (String Value): Set to ScreamRouter's IP address
   - `UnicastPort` (DWORD): Set to the port configured in ScreamRouter (default 4010)
4. Reboot your system.

## Integration with ScreamRouter

To use the Scream stream in ScreamRouter:

1. In your ScreamRouter configuration, add a new source with the following details:
   - Type: Scream
   - IP: The IP address of the Windows machine running Scream
   - Port: The port used for Scream streaming (default is 4010)

2. Create a route in ScreamRouter to connect this Scream source to your desired sink.

## Troubleshooting

- If ScreamRouter is not receiving audio, check that the IP addresses and port numbers match in both the Scream sender (Windows) and receiver (ScreamRouter) configurations.
- Ensure that your firewall allows UDP traffic on the specified port.
- For the desktop tool, check the Windows Services manager to ensure the Scream service is running.
- For the driver method, check Device Manager to ensure the Scream audio device is properly installed and not showing any errors.

For more information about ScreamRouter and its features, please refer to the [main README](../README.md) and other documentation files in the Readme directory.
