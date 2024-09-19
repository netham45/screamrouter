# VNC Implementation in ScreamRouter

ScreamRouter incorporates VNC (Virtual Network Computing) functionality to allow remote viewing and control of source devices. This feature enables users to manage their audio sources directly from the ScreamRouter interface.

## Overview

- VNC connections are initiated when the interface is loaded in the UI for a configured source.
- A WebSocket proxy is started on-demand to bridge the VNC connection from the ScreamRouter server.
- The VNC viewer runs in the browser using noVNC.

![Screenshot of ScreamRouter noVNC](/images/vnc.png)

## Configuration

To enable VNC for a source, add the following details to the Source Configuration:

- `vnc_ip`: IP address of the VNC server
- `vnc_port`: Port of the VNC server

## Using VNC in ScreamRouter

1. Configure VNC for a source as described above.
2. In the ScreamRouter interface, a 'VNC' button will appear for sources with VNC configured.
3. Click the 'VNC' button to open the noVNC viewer in a new window or tab.
4. Use the noVNC interface to view and control the source device.

## Troubleshooting

If you encounter issues with the VNC feature:

1. Ensure the VNC server is running on the source device and accessible from the ScreamRouter server.
2. Check that the `vnc_ip` and `vnc_port` are correctly configured in the source settings.
3. Verify that your browser supports WebSockets and is up to date.
4. Check the ScreamRouter logs for any VNC-related error messages.

## Security Considerations

- VNC traffic is not encrypted by default.
- Use strong passwords for your VNC servers to prevent unauthorized access.
- Limit VNC access to trusted networks and users.

For more information about ScreamRouter and its features, please refer to the [main README](../README.md) and other documentation files in the Readme directory.