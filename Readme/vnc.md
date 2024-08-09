# VNC Implementation

This project implements VNC (Virtual Network Computing) functionality to allow remote viewing and control of source devices. Here's an overview of how the VNC feature works:

## Overview

- VNC connections are started when the interface is loaded in the UI for a for configured source
- A WebSocket proxy is started on-demand to bridge the VNC connection, the connection comes from the ScreamRouter server
- The VNC viewer runs in the browser using noVNC

![Screenshot of ScreamRouter noVNC](/images/vnc.png)


## Configuration

VNC connection details are stored in the Source Configuration:

- `vnc_ip`: IP address of the VNC server
- `vnc_port`: Port of the VNC server