## Media Controls

ScreamRouter provides remote media control functionality, allowing users to send commands to audio sources for a seamless listening experience.

### Supported Commands
ScreamRouter can send the following media control commands to sources:
- Play/Pause
- Next Track
- Previous Track

### Command Transmission
These commands are transmitted as UDP packets to the audio sources:
- Next Track: 'n'
- Previous Track: 'p'
- Play/Pause: 'P'

### Technical Details
- Port: Commands are sent to port 9999 on the source
- Protocol: UDP
- Packet Content: Single character representing the command

### Receiver Implementation
The commands are currently received and processed by a bash script running within the media containers on the source devices. This script listens for incoming UDP packets on the specified port and executes the corresponding media control actions.

### User Interface Integration
Media control buttons will be visible and accessible in the ScreamRouter user interface, provided that a VNC host is configured. This allows users to control their media playback directly from the ScreamRouter interface, enhancing the overall user experience. If a source with VNC is selected in the UI the system's global media controls will control that source's media playback.

### Security
It's important to note that these commands are sent unencrypted and unauthenticated.
