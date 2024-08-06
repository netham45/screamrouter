# ScreamRouter Home Assistant Component

This Home Assistant component allows you to control a ScreamRouter instance directly from your Home Assistant setup. ScreamRouter is an audio routing system that enables flexible management of audio sources, routes, and sinks.

## Features

The component provides different control options depending on the type of ScreamRouter entry:

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

The component should be configurable through the Home Assistant UI. You'll need to provide:

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

## Support

For issues specific to the Home Assistant component, please open an issue in the ScreamRouter GitHub repository. For general ScreamRouter questions, refer to the main ScreamRouter documentation.

## Screenshots

![Screenshot of HA for ScreamRouter](/images/home_assistant.png)

This screenshot shows the ScreamRouter integration in action within Home Assistant, demonstrating the various control options available for audio sources, routes, and sinks.