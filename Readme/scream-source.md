# Scream Audio Streaming

Scream is a virtual audio driver and protocol for streaming audio over a network with very low latency. There are two options foer streaming from Windows, the original Scream Virtual Audio Driver or a user-mode transmitter that runs under your user. The user-mode transmitter is recommended due to ease of installation.

## Using the desktop tool

* You can download the desktop tool for sending Scream streams [here](https://github.com/netham45/windows-scream-sender)

To install run install_tool.bat and allow it to elevate to administrator and create a service under your user. Administrator access is required to allow the tool to set itself as real-mode priority.

You will be prompted for an IP to send to, a port to send to, and if you want to use Multicast. You can enter your ScreamRouter IP and Port and not use Multicast, or you can enter IP 239.255.77.77 and port 4010 with multicast enabled to send to multicast Scream receivers.

## Using the Driver

### Driver Installation on Windows 10 (version 1607 and newer)

#### Prerequisites
- To install the Scream driver the system clock must be set before July 7, 2023, which is the certificate's expiration date.

#### Configuring the OS
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

