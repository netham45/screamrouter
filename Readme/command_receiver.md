# ScreamRouter Media Controls

## Overview

ScreamRouter, a versatile audio routing and management system, provides remote media control functionality. This feature allows users to send commands to audio sources for a seamless listening experience, enhancing the overall usability of the system.

## Supported Commands

ScreamRouter supports the following media control commands:

- Next Track: 'n'
- Previous Track: 'p'
- Play/Pause: 'P'

These commands are transmitted as UDP packets to the audio sources.

## Technical Details

- **Port**: Commands are sent to port 9999 on the configured VNC server
- **Protocol**: UDP
- **Packet Content**: Single character representing the command

## User Interface Integration

Media control buttons are visible and accessible in the ScreamRouter user interface, provided that a VNC host is configured. This allows users to control their media playback directly from the ScreamRouter interface.

If a source with VNC is selected in the UI, the system's global media controls will control that source's media playback. This feature is also integrated with the Chrome App shortcuts, providing quick access to media controls when ScreamRouter is installed as a Progressive Web App (PWA).

## Implementation Scripts

### Bash Script (for Linux systems)

```bash
#!/bin/bash

netcat_command="nc -l -u -p 9999"
while x=x
do
  command=$(head -c 1 <($netcat_command))
  pkill -f "$netcat_command"
  echo Got command $command
  if [[ $command == "n" ]]
  then
    echo "Next song"
    xdotool key XF86AudioNext
  elif [[ $command == "p" ]]
  then
    echo "Previous song"
    xdotool key XF86AudioPrev 
  elif [[ $command == "P" ]]
  then
    echo "Play/Pause"
    xdotool key XF86AudioPlay  
  else
    echo "Unknown Command"
  fi
done
```

### PowerShell Script (for Windows systems)

For a complete solution with an installer to run on login, see the [win-scream-hotkey-receiver repository](https://github.com/netham45/win-scream-hotkey-receiver).

```powershell
$ShowWindowAsync = Add-Type -MemberDefinition '[DllImport("user32.dll")] public static extern bool ShowWindowAsync(IntPtr hWnd, int nCmdShow);' -name Win32ShowWindowAsync -namespace Win32Functions -PassThru
$KeybdEvent = Add-Type -MemberDefinition '[DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, int dwExtraInfo);' -name Win32KeybdEvent -namespace Win32Functions -PassThru

function SendKey($keyCode) {
    $KeybdEvent::keybd_event($keyCode, 0, 0x01, 0)  # KEYEVENTF_EXTENDEDKEY
    $KeybdEvent::keybd_event($keyCode, 0, 0x03, 0)  # KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP
}

$ShowWindowAsync::ShowWindowAsync((Get-Process -PID $pid).MainWindowHandle, 0) # Hide the window
$socket = New-Object System.Net.Sockets.UdpClient # Create a new UDP Client
$socket.ExclusiveAddressUse = $false # Allow multiple sockets to bind to the port
$socket.Client.Bind((New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any, 9999))) # Bind to port 9999

while ($receivedData = [System.Text.Encoding]::ASCII.GetString($socket.Receive([ref]$null))) {
    if     ($receivedData -ceq 'n') {SendKey 0xB0} # VK_MEDIA_NEXT_TRACK
    elseif ($receivedData -ceq 'p') {SendKey 0xB1} # VK_MEDIA_PREV_TRACK
    elseif ($receivedData -ceq 'P') {SendKey 0xB3} # VK_MEDIA_PLAY_PAUSE
}
```