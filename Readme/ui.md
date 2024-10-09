# ScreamRouter User Interface Guide

This guide provides an overview of the ScreamRouter user interface, explaining how to manage audio sources, sinks, routes, and utilize various features for optimal audio routing and control.

## Interface Overview

![Screenshot of ScreamRouter Dashboard](/images/Dashboard%20Full.png)

The ScreamRouter interface is designed to be user-friendly and intuitive. As you can see in the dashboard image above, it consists of several main sections:
- Sources (left panel): Manage your audio inputs
- Routes (center panel): Control how audio flows from sources to sinks
- Sinks (right panel): Manage your audio outputs
- Active Source (top): Quick access to the currently selected source
- Visualizer (bottom): Real-time visual representation of your audio
- Search bar (top): Quickly find sources, sinks, or routes
- Favorites: Quick access to your most-used components (not shown in the image)

## Dashboard

The Dashboard is your home screen in ScreamRouter. It provides:
- A comprehensive view of your active sources, sinks, and routes
- Easy access to your most frequently used controls
- The audio visualizer for real-time audio representation
- A search function to quickly find what you need

## Managing Audio Components

### Sources
![Sources Interface](/images/Sources.png)

Sources are where your audio comes from. To manage sources:

1. Look at the left panel of the dashboard.
2. To add a new source, click the "Add Source" button.

![Add Source Interface](/images/AddSource.png)

3. Fill in the required information (Name, IP address, etc.).
4. Adjust volume and equalizer settings as needed.
5. Use the "Enable/Disable" toggle to turn the source on or off.

### Sinks
![Sinks Interface](/images/Sinks.png)

Sinks are where your audio goes. To manage sinks:

1. Look at the right panel of the dashboard.
2. To add a new sink, click the "Add Sink" button.

![Add Sink Interface](/images/AddSink.png)

3. Provide the necessary details (Name, IP address, port, etc.).
4. Configure audio format settings if required.
5. Use the "Enable/Disable" toggle to turn the sink on or off.

### Routes

![Routes Interface](/images/Routes.png)

Routes determine how audio flows from sources to sinks. To create or edit a route:

1. Look at the center panel of the dashboard.
2. Click "Add Route" to create a new route.

![Add Route Interface](/images/AddRoute.png)

3. Select a source and a sink for the route.
4. Adjust volume, equalizer, and delay settings as needed.
5. Enable or disable the route using the toggle switch.

### Groups

Groups allow you to manage multiple sources or sinks together. To create a group:

1. In either the Sources or Sinks panel, click the "Create Group" button.
2. Select the individual components you want to include in the group.
3. Give your group a name and save it.

![Group Management](/images/Groups.png)

Now you can control all items in the group at once!

## Audio Enhancement Features

### Equalizer

The equalizer allows you to fine-tune your audio:

1. Find the equalizer controls on source, sink, or route cards.
2. Adjust the sliders to boost or cut different frequency ranges.
3. Create custom sound profiles for different types of audio.

![Equalizer Interface](/images/Equalizer.png)

### Visualizer

Enjoy a visual representation of your audio:

1. Look for the visualizer at the bottom of the dashboard.
2. Click on the visualizer to enter fullscreen mode.
3. Press 'H' to cycle through different visualization styles.

### Audio Controls

Easy-to-use audio controls are available for sources and routes:

- Play/Pause: Start or stop audio playback
- Next/Previous: Skip between tracks (if supported by the source)
- Volume: Adjust the loudness
- Timeshift: Rewind or fast-forward through the audio buffer (up to 5 minutes)

## Additional Features

### Active Source

The Active Source section at the top of the dashboard provides quick access to controls for the currently selected audio source. Here you can adjust volume, apply equalizer settings, and use playback controls without navigating away from your current view.

### Search Functionality

Use the search bar at the top of the dashboard to quickly find sources, sinks, or routes by name. This is especially useful when you have many audio components configured.

### Connection View

See how your audio is routed:

- On desktop: Lines connect sources to sinks in the dashboard, showing active connections.
- On mobile: A list view displays connections.
- Hover over lines to see connection details.
- Click on lines to quickly activate that source and sink.

### VNC Integration

Control your sources remotely:

1. Set up VNC for a source in its settings.
2. Click the 'VNC' button that appears to open the remote control window.

See [the VNC documentation](/Readme/vnc.md) for more details.

### Media Keys

![Media Key Controls](/images/MediaKeys.png)

When VNC is enabled for a source:
- Media control buttons appear under the source card.
- Your device's media keys can control the source.

### Now Playing

The Now Playing feature shows information about the currently playing audio on a source or sink. This includes details like the track name, artist, and playback progress when available.

### Dark Mode

![Dark Mode](/images/Dark%20Mode.png)

Prefer a darker interface? Enable dark mode:

1. Look for the dark mode toggle in the app's settings or menu.
2. Click to switch between light and dark themes.

### Favorites

The Favorites feature allows you to bookmark your most-used sources, sinks, or routes for quick access. To add an item to your favorites:

1. Look for the star icon next to the item you want to favorite.
2. Click the star to add it to your favorites.
3. Access your favorites from the dedicated Favorites section for easy management.

## Layout and Responsive Design

ScreamRouter adapts to your device:
- On desktop: See all sections at once for easy management, as shown in the dashboard image.
- On mobile: Collapsible sections keep things tidy on smaller screens.

## Keyboard Shortcuts

ScreamRouter supports various keyboard shortcuts to enhance your productivity:

- Space: Play/Pause the active source
- Left/Right Arrow: Previous/Next track (if supported by the source)
- Up/Down Arrow: Adjust volume
- M: Mute/Unmute
- H: Cycle through visualizer styles (when visualizer is in focus)

For a complete list of keyboard shortcuts, refer to the settings menu in the application.

## Troubleshooting

If you run into issues:

1. Try refreshing your browser or reloading the ScreamRouter application.
2. Clear your browser cache and cookies, then reload the page.
3. Check your internet connection and ensure ScreamRouter server is running.
4. Verify that all your audio devices are properly connected and powered on.
5. Check the status indicators on sources and sinks to ensure they're online and configured correctly.
6. If you're having issues with a specific route, try disabling and re-enabling it.
7. For VNC-related issues, ensure your VNC server is running and accessible.
8. If audio quality is poor, check your network conditions and adjust buffer settings if necessary.
9. Consult the error logs in the application settings for more detailed information about any issues.
10. If problems persist, check our troubleshooting guide in the main documentation or contact support.

For more information about ScreamRouter and its features, please refer to the [main README](../README.md) and other documentation files in the Readme directory.
