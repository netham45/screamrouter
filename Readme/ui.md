# ScreamRouter User Guide

ScreamRouter is a tool for routing audio around your house, allowing you to manage multiple audio sources and sinks with customizable routing and mixing capabilities.

## Table of Contents
1. [Interface Overview](#interface-overview)
2. [Managing Sources](#managing-sources)
3. [Managing Sinks](#managing-sinks)
6. [Managing Groups](#groups)
4. [Creating and Managing Routes](#creating-and-managing-routes)
5. [Using the Equalizer](#using-the-equalizer)
7. [Listening to Sinks](#listen)
8. [Visualizer](#visualizer)
9. [Timeshifting](#timeshifting)
9. [VNC](#vnc)
10. [Media Keys](#media-keys)

## Interface Overview

![Screenshot of ScreamRouter](/images/ScreamRouter.png)

The ScreamRouter interface consists of three main sections:
- Sources (left panel)
- Routes (center panel)
- Sinks (right panel)

Each section allows you to add, edit, and manage its respective components.

## Managing Sources

![Screenshot of ScreamRouter Add/Edit Source Interface](/images/AddSource.png)

Sources represent audio inputs to the system. To manage sources:
1. Use the "Add Source" button in the Sources panel.
2. Fill in the required information (Name, IP address, etc.).
3. Adjust volume and equalizer settings as needed.
4. Use the "Enable/Disable" toggle to activate or deactivate a source.

## Managing Sinks

![Screenshot of ScreamRouter Add/Edit Sink Interface](/images/AddSink.png)

Sinks represent audio outputs. To manage sinks:
1. Use the "Add Sink" button in the Sinks panel.
2. Provide necessary details (Name, IP address, port, etc.).
3. Configure audio format settings if required.
4. Use the "Enable/Disable" toggle to activate or deactivate a sink.

### Groups

![Screenshot of ScreamRouter Group Card](/images/Groups.png)

You can create groups of sources or sinks for easier management:
1. Use the "Add Group" option in the respective panel.
2. Select the individual components to include in the group.
3. Apply settings to the entire group at once.


## Creating and Managing Routes

![Screenshot of ScreamRouter Route Editor Interface](/images/RouteEditor.png)

Routes define connections between sources and sinks. To create a route either:
1. Select a source from the Sources panel.
2. Select a sink from the Sinks panel.
3. Use the Routes panel to configure:
   - Volume adjustment
   - Equalizer settings
   - Delay adjustment
   - Enabling/disabling routes -- Routes will automatically be created when enabled if they don't already exist.

Or, use the "Edit Routes" option available:
1. Select a source or sink.
2. Select "Edit Routes".
3. Choose to Enable or Disable the sources or sinks you want connected to your choice
4. Save the configuration

## Using the Equalizer

![Screenshot of ScreamRouter Equalizer Interface](/images/Equalizer.png)

The Equalizer feature allows you to adjust gain for 18 bands at various points:
1. For individual sources or sinks
2. For routes
3. For groups of sources or sinks

Equalizer settings stack across each source/route/sink/group the audio passes through, allowing for fine-tuned audio control.

## Routing View

![Screenshot of ScreamRouter Route View](/images/RouteView.png)

On desktop the routing view will be lines connecting sources to sinks representing each active connection. They can be hovered over to show the connection or clicked on to activate the source and sink on either side of the route.

Routes that are not associated with currently selected sinks/sources will be displayed as black lines, routes associated with selected sources/sinks will be displayed as green.

On mobile the routing view will be a list instead of connecting lines.

### Listen 
There is the ability to listen to the output going to any sink. A sink can be pointed to a bogus IP to be exclusively used through it's MP3 stream. Currently only stereo sinks are supported.

### Visualizer

![Screenshot of ScreamRouter with Butterchurn running in background](/images/Visualizer.png)

The MP3 stream can be feed into Butterchurn/Milkdrop to have visual effects generated off of the music. To fullscreen the visualizer click on the background. To cycle through visualizations press 'H'.

### Timeshifting

ScreamRouter will buffer the last five minutes of audio and allows it to be scrubbed through. The timeshift controls are available on any source/route/sink/group and affect all audio that passes through that path.

### VNC

![Screenshot of ScreamRouter VNC Client](/images/vnc.png)

There is a built-in noVNC viewer that can be used to control sources from the ScreamRouter interface. To open the VNC client configure VNC for a source and click on the 'VNC' button that appears.

See [the VNC documentation](/Readme/vnc.md) for more information.


### Media Keys

![Screenshot of ScreamRouter Media Keys](/images/MediaKeys.png)

When VNC is enabled for a source media keys will appear under the card, and the global media hotkeys will send control commands to the source.

See [the Command Receiver documentation](/Readme/command_receiver.md) for more information on how media key commands work.
