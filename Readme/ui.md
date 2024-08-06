# ScreamRouter User Guide

ScreamRouter is a powerful tool for routing audio around your house, allowing you to manage multiple audio sources and sinks with customizable routing and mixing capabilities.

## Table of Contents
1. [Interface Overview](#interface-overview)
2. [Managing Sources](#managing-sources)
3. [Managing Sinks](#managing-sinks)
4. [Creating and Managing Routes](#creating-and-managing-routes)
5. [Using the Equalizer](#using-the-equalizer)
6. [Advanced Features](#advanced-features)

## Interface Overview

The ScreamRouter interface consists of three main sections:
- Sources (left panel)
- Routes (center panel)
- Sinks (right panel)

Each section allows you to add, edit, and manage its respective components.

## Managing Sources

Sources represent audio inputs to the system. To manage sources:
1. Use the "Add Source" button in the Sources panel.
2. Fill in the required information (Name, IP address, etc.).
3. Adjust volume and equalizer settings as needed.
4. Use the "Enable/Disable" toggle to activate or deactivate a source.

## Managing Sinks

Sinks represent audio outputs. To manage sinks:
1. Use the "Add Sink" button in the Sinks panel.
2. Provide necessary details (Name, IP address, port, etc.).
3. Configure audio format settings if required.
4. Use the "Enable/Disable" toggle to activate or deactivate a sink.

## Creating and Managing Routes

Routes define connections between sources and sinks. To create a route:
1. Select a source from the Sources panel.
2. Select a sink from the Sinks panel.
3. Use the Routes panel to configure:
   - Volume adjustment
   - Equalizer settings
   - Delay adjustment

## Using the Equalizer

The Equalizer feature allows you to adjust audio settings at various levels:
1. For individual sources or sinks
2. For routes
3. For groups of sources or sinks

Equalizer settings stack, allowing for fine-tuned audio control.

## Advanced Features

### Groups
You can create groups of sources or sinks for easier management:
1. Use the "Add Group" option in the respective panel.
2. Select the individual components to include in the group.
3. Apply settings to the entire group at once.

### Links
The central panel provides a visual representation of your audio routing setup, helping you understand and manage complex configurations.

### Listen 

There is the ability to listen to the output going to any sink. A sink can be pointed to a bogus IP to be exclusively used through it's MP3 stream. Currently only stereo sinks are supported.

### Visualizer

The MP3 stream can be feed into Butterchurn/Milkdrop to have visual effects generated off of the music.

---

For more detailed information on configuration and advanced usage, please refer to the configuration.md file in the documentation.
