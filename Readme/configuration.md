# ScreamRouter Configuration

The ScreamRouter system manages audio routing between sources and sinks using three main components:

1. Sources
2. Sinks 
3. Routes

## Sources

Sources represent audio inputs to the system. They can be:

- Individual sources (e.g. a specific computer or device)
- Source groups (collections of individual sources)

Key properties:
- Name
- IP address (for individual sources)
- Volume
- Equalizer settings (18-band)
- Delay

## Sinks 

Sinks represent audio outputs from the system. They can be:

- Individual sinks (e.g. a specific speaker or audio device)
- Sink groups (collections of individual sinks)

Key properties:
- Name
- IP address and port (for individual sinks)
- Volume
- Equalizer settings (18-band)
- Delay
- Audio format settings (bit depth, sample rate, channels)
  - Supported sample rates: 44100, 48000, 88200, 96000, and 192000 Hz

## Routes

Routes define connections between sources and sinks. A route specifies:

- A source (individual or group)
- A sink (individual or group)
- Volume adjustment
- Equalizer adjustment (18-band)
- Delay adjustment

## Interaction

The ConfigurationSolver class handles the logic of resolving the connections between sources and sinks:

1. It expands source and sink groups into their individual components.
2. It applies volume, equalizer, and delay adjustments from routes, groups, and individual components.
3. It creates a final mapping of real (individual) sinks to lists of real sources.

This allows for flexible configuration:
- Sources can be routed to multiple sinks
- Sinks can receive audio from multiple sources
- Groups allow for easy management of multiple sources or sinks
- Adjustments can be applied at the route, group, or individual level

The ConfigurationManager class uses this solved configuration to set up and manage the actual audio controllers and routing.

## Configuration File

ScreamRouter uses a YAML-based configuration file to store all settings. The system automatically saves changes to this file whenever configuration updates are made, ensuring that your setup is always up-to-date and persistent across restarts.
