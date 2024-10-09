# ScreamRouter User Interface Guide

This guide provides an overview of the ScreamRouter user interface, explaining how to manage audio sources, sinks, routes, and utilize various features for optimal audio routing and control.

## Interface Overview

![Screenshot of ScreamRouter](/images/ScreamRouter.png)

The ScreamRouter interface consists of several main sections:
- Dashboard (main view)
- Sources
- Sinks
- Routes
- Active Source
- Favorites

## Managing Audio Components

### Sources

To manage sources:
1. Navigate to the Sources section.
2. Click "Add Source" to create a new source.
3. Fill in the required information (Name, IP address, etc.).
4. Adjust volume and equalizer settings as needed.
5. Use the "Enable/Disable" toggle to activate or deactivate a source.

### Sinks

To manage sinks:
1. Navigate to the Sinks section.
2. Click "Add Sink" to create a new sink.
3. Provide necessary details (Name, IP address, port, etc.).
4. Configure audio format settings if required.
5. Use the "Enable/Disable" toggle to activate or deactivate a sink.

### Groups

Create groups of sources or sinks for easier management:
1. Click the Create Group in the respective section (Sources or Sinks).
2. Select the individual components to include in the group.
3. Apply settings to the entire group at once.

### Routes

To create or edit a route:
1. Navigate to the Routes section.
2. Use the AddEditRoute component to configure the route.
3. Select a source and a sink for the route.
4. Configure volume, equalizer, and delay settings.
5. Enable/disable routes as needed.

## Audio Enhancement Features

### Equalizer

The Equalizer allows you to adjust gain for 18 bands at various points:
1. For individual sources or sinks
2. For routes
3. For groups of sources or sinks

### Visualizer

![Screenshot of ScreamRouter with Butterchurn running in background](/images/Visualizer.png)

The MP3 stream can be fed into Butterchurn/Milkdrop for visual effects:
- Click on the background to fullscreen the visualizer.
- Press 'H' to cycle through visualizations.

### Audio Controls

The AudioControls component provides various playback controls:
- Play/Pause
- Next/Previous track
- Volume adjustment
- Timeshift controls for audio seeking (ScreamRouter buffers the last five minutes of audio)

## Additional Features


### VNC Integration

The VNC component allows remote control of sources:
1. Configure VNC for a source in the source settings.
2. Use the built-in noVNC viewer to control the source remotely.

See [the VNC documentation](/Readme/vnc.md) for more information.

### Now Playing

The NowPlaying component displays information about the currently playing audio on a source or sink.

### Favorites

The FavoriteSection component allows users to quickly access their most-used sources, sinks, or routes.

### Dark Mode

A dark mode option is available for the user interface, providing a different color scheme for low-light environments or personal preference.

## Layout and Responsive Design

The Layout component ensures that the user interface is responsive and adapts to different screen sizes:
- Desktop view: Full layout with all sections visible
- Mobile view: Collapsible sections for easy navigation on smaller screens

## Troubleshooting

If you encounter issues with the user interface:
1. Ensure your browser is up to date.
2. Clear your browser cache and reload the page.
3. Check your network connection to the ScreamRouter server.
4. Verify that all sources and sinks are properly configured and online.
5. Check the browser console for any error messages.

For more detailed information about ScreamRouter and its features, please refer to the [main README](../README.md) and other documentation files in the Readme directory.
