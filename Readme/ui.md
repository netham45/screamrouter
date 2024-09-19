# ScreamRouter User Interface Guide

This guide provides an overview of the ScreamRouter user interface, explaining how to manage audio sources, sinks, routes, and utilize various features for optimal audio routing and control.

## Interface Overview

![Screenshot of ScreamRouter](/images/ScreamRouter.png)

The ScreamRouter interface consists of three main sections:
- Sources (left panel)
- Routes (center panel)
- Sinks (right panel)

## Managing Audio Components

### Sources

![Screenshot of ScreamRouter Add/Edit Source Interface](/images/AddSource.png)

To manage sources:
1. Click "Add Source" in the Sources panel.
2. Fill in the required information (Name, IP address, etc.).
3. Adjust volume and equalizer settings as needed.
4. Use the "Enable/Disable" toggle to activate or deactivate a source.

### Sinks

![Screenshot of ScreamRouter Add/Edit Sink Interface](/images/AddSink.png)

To manage sinks:
1. Click "Add Sink" in the Sinks panel.
2. Provide necessary details (Name, IP address, port, etc.).
3. Configure audio format settings if required.
4. Use the "Enable/Disable" toggle to activate or deactivate a sink.

### Groups

![Screenshot of ScreamRouter Group Card](/images/Groups.png)

Create groups of sources or sinks for easier management:
1. Use the "Add Group" option in the respective panel.
2. Select the individual components to include in the group.
3. Apply settings to the entire group at once.

### Routes

![Screenshot of ScreamRouter Route Editor Interface](/images/RouteEditor.png)

To create a route:
1. Select a source from the Sources panel.
2. Select a sink from the Sinks panel.
3. Use the Routes panel to configure volume, equalizer, and delay settings.
4. Enable/disable routes as needed.

Alternatively, use the "Edit Routes" option:
1. Select a source or sink.
2. Click "Edit Routes".
3. Choose to Enable or Disable the desired connections.
4. Save the configuration.

## Audio Enhancement Features

### Equalizer

![Screenshot of ScreamRouter Equalizer Interface](/images/Equalizer.png)

Adjust gain for 18 bands at various points:
1. For individual sources or sinks
2. For routes
3. For groups of sources or sinks

### Visualizer

![Screenshot of ScreamRouter with Butterchurn running in background](/images/Visualizer.png)

The MP3 stream can be fed into Butterchurn/Milkdrop for visual effects:
- Click on the background to fullscreen the visualizer.
- Press 'H' to cycle through visualizations.

### Timeshifting

ScreamRouter buffers the last five minutes of audio, allowing for audio seeking:
- Timeshift controls are available on any source/route/sink/group.
- Affects all audio that passes through that path.

## Additional Features

### Routing View

![Screenshot of ScreamRouter Route View](/images/RouteView.png)

- On desktop: Lines connect sources to sinks, representing active connections.
- On mobile: A list view of connections is displayed.
- Hover over lines to show connection details.
- Click on lines to activate the associated source and sink.

### Listen to Sinks

- Listen to the output going to any sink via MP3 stream.
- Configure a sink with a bogus IP for exclusive use through its MP3 stream.

### VNC Integration

![Screenshot of ScreamRouter VNC Client](/images/vnc.png)

Use the built-in noVNC viewer to control sources:
1. Configure VNC for a source.
2. Click the 'VNC' button that appears.

See [the VNC documentation](/Readme/vnc.md) for more information.

### Media Keys

![Screenshot of ScreamRouter Media Keys](/images/MediaKeys.png)

When VNC is enabled for a source:
- Media keys appear under the card.
- Global media hotkeys send control commands to the source.

See [the Command Receiver documentation](/Readme/command_receiver.md) for more information.

## Troubleshooting

If you encounter issues with the user interface:
1. Ensure your browser is up to date.
2. Clear your browser cache and reload the page.
3. Check your network connection to the ScreamRouter server.
4. Verify that all sources and sinks are properly configured and online.

For more detailed information about ScreamRouter and its features, please refer to the [main README](../README.md) and other documentation files in the Readme directory.
