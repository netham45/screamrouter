# ScreamRouter User Interface Guide

This guide provides an overview of the ScreamRouter user interface, explaining how to manage audio sources, sinks, routes, and utilize various features for optimal audio routing and control.

## Interface Overview

![Screenshot of ScreamRouter](/images/ScreamRouter.png)

The ScreamRouter interface is designed to be user-friendly and intuitive. It consists of several main sections:
- Dashboard: Your central control panel
- Sources: Manage your audio inputs
- Sinks: Manage your audio outputs
- Routes: Control how audio flows from sources to sinks
- Active Source: Quick access to the currently selected source
- Favorites: Your most-used audio components for easy access

## Managing Audio Components

### Sources

Sources are where your audio comes from. To manage sources:

1. Click on the "Sources" section in the main menu.
2. To add a new source, click the "Add Source" button.

![Add Source Interface](/images/AddSource.png)

3. Fill in the required information (Name, IP address, etc.).
4. Adjust volume and equalizer settings as needed.
5. Use the "Enable/Disable" toggle to turn the source on or off.

### Sinks

Sinks are where your audio goes. To manage sinks:

1. Click on the "Sinks" section in the main menu.
2. To add a new sink, click the "Add Sink" button.

![Add Sink Interface](/images/AddSink.png)

3. Provide the necessary details (Name, IP address, port, etc.).
4. Configure audio format settings if required.
5. Use the "Enable/Disable" toggle to turn the sink on or off.

### Groups

Groups allow you to manage multiple sources or sinks together. To create a group:

1. In either the Sources or Sinks section, click the "Create Group" button.
2. Select the individual components you want to include in the group.
3. Give your group a name and save it.

![Group Management](/images/Groups.png)

Now you can control all items in the group at once!

### Routes

Routes determine how audio flows from sources to sinks. To create or edit a route:

1. Go to the "Routes" section in the main menu.
2. Click "Add Route" to create a new route.

![Add Route Interface](/images/AddRoute.png)

3. Select a source and a sink for the route.
4. Adjust volume, equalizer, and delay settings as needed.
5. Enable or disable the route using the toggle switch.

## Audio Enhancement Features

### Equalizer

The equalizer allows you to fine-tune your audio:

1. Find the equalizer controls on source, sink, or route pages.
2. Adjust the sliders to boost or cut different frequency ranges.
3. Create custom sound profiles for different types of audio.

![Equalizer Interface](/images/Equalizer.png)

### Visualizer

Enjoy a visual representation of your audio:

1. Look for the visualizer on the main dashboard or audio control pages.
2. Click on the visualizer to enter fullscreen mode.
3. Press 'H' to cycle through different visualization styles.

![Audio Visualizer](/images/Visualizer.png)

### Audio Controls

Easy-to-use audio controls are available for sources and routes:

- Play/Pause: Start or stop audio playback
- Next/Previous: Skip between tracks (if supported by the source)
- Volume: Adjust the loudness
- Timeshift: Rewind or fast-forward through the audio buffer (up to 5 minutes)

## Additional Features

### Connection View

See how your audio is routed:

- On desktop: Lines connect sources to sinks, showing active connections.
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

### Dark Mode

![Dark Mode](/images/Dark Mode.png)

Prefer a darker interface? Enable dark mode:

1. Look for the dark mode toggle in the app's settings or menu.
2. Click to switch between light and dark themes.

## Layout and Responsive Design

ScreamRouter adapts to your device:
- On desktop: See all sections at once for easy management.
- On mobile: Collapsible sections keep things tidy on smaller screens.

## Troubleshooting

If you run into issues:

1. Try refreshing your browser.
2. Clear your browser cache and reload.
3. Check your internet connection.
4. Make sure all your audio devices are properly connected and powered on.
5. If problems persist, check our troubleshooting guide or contact support.

For more information about ScreamRouter and its features, please refer to the [main README](../README.md) and other documentation files in the Readme directory.
