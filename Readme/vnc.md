# VNC Implementation

This project implements VNC (Virtual Network Computing) functionality to allow remote viewing and control of source devices. Here's an overview of how the VNC feature works:

## Overview

- VNC connections are initiated from the web interface for configured source devices
- A WebSocket proxy is started on-demand to bridge the VNC connection
- The VNC viewer runs in the browser using noVNC

![Screenshot of ScreamRouter noVNC](/images/vnc.png)

## Key Components

1. `APIWebsite` class in `api_website.py`:
   - Handles the VNC endpoint and starts the WebSocket proxy
   - Serves the VNC viewer HTML/JS

2. `websockify`: 
   - Used to create a WebSocket proxy to the VNC server

3. noVNC:
   - HTML5 VNC client that runs in the browser

4. `vnc.html.jinja` template:
   - Renders the VNC viewer iframe

## Flow

1. User requests VNC connection for a source
2. `vnc()` method in `APIWebsite` is called
3. A new WebSocket proxy is started on a unique port
4. The VNC viewer HTML is returned, configured with the proxy port
5. Browser loads the noVNC client, which connects via WebSocket
6. WebSocket proxy forwards traffic to/from the actual VNC server

## Configuration

VNC connection details are stored in the Source Configuration:

- `vnc_ip`: IP address of the VNC server
- `vnc_port`: Port of the VNC server

This VNC implementation allows convenient in-browser remote access to source devices, integrating smoothly with the rest of the application's interface.
