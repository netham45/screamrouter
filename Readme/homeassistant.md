# ScreamRouter Home Assistant Integration

## Introduction

The ScreamRouter Home Assistant integration allows you to control your ScreamRouter instance directly from your Home Assistant setup. This integration brings the power and flexibility of ScreamRouter's audio routing system into your smart home environment, enabling seamless control of audio sources, routes, and sinks through Home Assistant's interface and automation capabilities.

[Go to the component's repository here](https://github.com/netham45/screamrouter_ha_component)

## Features

The integration provides different control options depending on the type of ScreamRouter entry:

### Sources

- Volume control
- Play/Pause functionality
- Previous/Next/Play-Pause commands for media players (if ScreamRouter media control is configured)

### Routes

- Volume control
- Play/Pause functionality

### Sinks

- Volume control
- Play/Pause functionality
- Ability to play audio to sinks using Home Assistant media

## Installation

1. Copy the `screamrouter_ha_component` folder to your Home Assistant `custom_components` directory.
2. Restart Home Assistant.
3. Go to Configuration > Integrations in the Home Assistant UI.
4. Click the "+ ADD INTEGRATION" button and search for "ScreamRouter".
5. Follow the setup wizard to configure your ScreamRouter instance.

## Configuration

The integration is configurable through the Home Assistant UI. You'll need to provide:

- The IP address or hostname of your ScreamRouter instance

## Usage

Once configured, ScreamRouter entities will appear in your Home Assistant dashboard. You can:

- Control volume using sliders
- Toggle play/pause for sources, routes, and sinks
- Use media player controls for sources (if configured)
- Play Home Assistant media to ScreamRouter sinks

## Integrating with Home Assistant

You can use ScreamRouter entities in your automations, scripts, and scenes. For example:

- Automatically lower the volume of a specific audio route when a door opens
- Create a "Movie Night" scene that sets up your audio routing and adjusts volumes
- Use voice commands to control your audio setup through ScreamRouter


## Troubleshooting

If you encounter issues:

1. Check that your ScreamRouter instance is running and accessible from your Home Assistant server.
2. Verify that the component is correctly installed in your `custom_components` directory.
3. Review the Home Assistant logs for any error messages related to ScreamRouter.

## Screenshots

![Screenshot of HA for ScreamRouter](/images/HAMediaPlayer.png)

For more information about ScreamRouter and its features, please refer to the [main README](../README.md) and other documentation files in the Readme directory.