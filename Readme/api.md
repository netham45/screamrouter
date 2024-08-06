# ScreamRouter API Documentation

ScreamRouter is an application for routing PCM audio between Scream sinks and sources. This README provides detailed information about the API endpoints, their functionality, limitations, and data types.

## Table of Contents

1. [Sink Configuration](#sink-configuration)
2. [Source Configuration](#source-configuration)
3. [Route Configuration](#route-configuration)
4. [Streaming](#streaming)
5. [Site Resources](#site-resources)

## Sink Configuration

### Get Sinks

- **Endpoint**: GET `/sinks`
- **Description**: Returns a list of all configured sinks.
- **Response**: Array of `SinkDescription` objects.

### Add Sink

- **Endpoint**: POST `/sinks`
- **Description**: Adds a new sink or sink group.
- **Request Body**: `SinkDescription` object
- **Response**: Boolean indicating success or failure.

### Update Sink

- **Endpoint**: PUT `/sinks/{old_sink_name}`
- **Description**: Updates fields on an existing sink. Undefined fields are ignored.
- **Path Parameter**: 
  - `old_sink_name` (string): Name of the sink to update
- **Request Body**: `SinkDescription` object with updated fields
- **Response**: Boolean indicating success or failure.

### Delete Sink

- **Endpoint**: DELETE `/sinks/{sink_name}`
- **Description**: Deletes a sink by name.
- **Path Parameter**: 
  - `sink_name` (string): Name of the sink to delete
- **Response**: Boolean indicating success or failure.

### Enable/Disable Sink

- **Endpoints**: 
  - GET `/sinks/{sink_name}/enable`
  - GET `/sinks/{sink_name}/disable`
- **Description**: Enables or disables a sink by name.
- **Path Parameter**: 
  - `sink_name` (string): Name of the sink to enable/disable
- **Response**: Boolean indicating success or failure.

### Update Sink Volume

- **Endpoint**: GET `/sinks/{sink_name}/volume/{volume}`
- **Description**: Sets the volume for a sink or sink group.
- **Path Parameters**: 
  - `sink_name` (string): Name of the sink
  - `volume` (float): Volume level (0.0 to 1.0)
- **Response**: Boolean indicating success or failure.

### Update Sink Equalizer

- **Endpoint**: POST `/sinks/{sink_name}/equalizer/`
- **Description**: Sets the equalizer for a sink or sink group.
- **Path Parameter**: 
  - `sink_name` (string): Name of the sink
- **Request Body**: `Equalizer` object
- **Response**: Boolean indicating success or failure.

### Update Sink Position

- **Endpoint**: GET `/sinks/{sink_name}/reorder/{new_index}`
- **Description**: Sets the position of a sink in the list of sinks.
- **Path Parameters**: 
  - `sink_name` (string): Name of the sink
  - `new_index` (integer): New position in the list
- **Response**: Unspecified (likely a success indicator)

## Source Configuration

### Get Sources

- **Endpoint**: GET `/sources`
- **Description**: Returns a list of all configured sources.
- **Response**: Array of `SourceDescription` objects.

### Add Source

- **Endpoint**: POST `/sources`
- **Description**: Adds a new source or source group.
- **Request Body**: `SourceDescription` object
- **Response**: Boolean indicating success or failure.

### Update Source

- **Endpoint**: PUT `/sources/{old_source_name}`
- **Description**: Updates fields on an existing source. Undefined fields are not changed.
- **Path Parameter**: 
  - `old_source_name` (string): Name of the source to update
- **Request Body**: `SourceDescription` object with updated fields
- **Response**: Boolean indicating success or failure.

### Delete Source

- **Endpoint**: DELETE `/sources/{source_name}`
- **Description**: Deletes a source by name.
- **Path Parameter**: 
  - `source_name` (string): Name of the source to delete
- **Response**: Boolean indicating success or failure.

### Enable/Disable Source

- **Endpoints**: 
  - GET `/sources/{source_name}/enable`
  - GET `/sources/{source_name}/disable`
- **Description**: Enables or disables a source by name.
- **Path Parameter**: 
  - `source_name` (string): Name of the source to enable/disable
- **Response**: Boolean indicating success or failure.

### Update Source Volume

- **Endpoint**: GET `/sources/{source_name}/volume/{volume}`
- **Description**: Sets the volume for a source or source group.
- **Path Parameters**: 
  - `source_name` (string): Name of the source
  - `volume` (float): Volume level (0.0 to 1.0)
- **Response**: Boolean indicating success or failure.

### Update Source Equalizer

- **Endpoint**: POST `/sources/{source_name}/equalizer`
- **Description**: Sets the equalizer for a source or source group.
- **Path Parameter**: 
  - `source_name` (string): Name of the source
- **Request Body**: `Equalizer` object
- **Response**: Boolean indicating success or failure.

### Source Playback Controls

- **Endpoints**: 
  - GET `/sources/{source_name}/play`
  - GET `/sources/{source_name}/nexttrack`
  - GET `/sources/{source_name}/prevtrack`
- **Description**: Send playback control commands to the source.
- **Path Parameter**: 
  - `source_name` (string): Name of the source
- **Response**: Boolean indicating success or failure.

### Update Source Position

- **Endpoint**: GET `/sources/{source_name}/reorder/{new_index}`
- **Description**: Sets the position of a source in the list of sources.
- **Path Parameters**: 
  - `source_name` (string): Name of the source
  - `new_index` (integer): New position in the list
- **Response**: Unspecified (likely a success indicator)

## Route Configuration

### Get Routes

- **Endpoint**: GET `/routes`
- **Description**: Returns a list of all configured routes.
- **Response**: Array of `RouteDescription` objects.

### Add Route

- **Endpoint**: POST `/routes`
- **Description**: Adds a new route.
- **Request Body**: `RouteDescription` object
- **Response**: Boolean indicating success or failure.

### Update Route

- **Endpoint**: PUT `/routes/{old_route_name}`
- **Description**: Updates fields on an existing route. Undefined fields are ignored.
- **Path Parameter**: 
  - `old_route_name` (string): Name of the route to update
- **Request Body**: `RouteDescription` object with updated fields
- **Response**: Boolean indicating success or failure.

### Delete Route

- **Endpoint**: DELETE `/routes/{route_name}`
- **Description**: Deletes a route by name.
- **Path Parameter**: 
  - `route_name` (string): Name of the route to delete
- **Response**: Boolean indicating success or failure.

### Enable/Disable Route

- **Endpoints**: 
  - GET `/routes/{route_name}/enable`
  - GET `/routes/{route_name}/disable`
- **Description**: Enables or disables a route by name.
- **Path Parameter**: 
  - `route_name` (string): Name of the route to enable/disable
- **Response**: Boolean indicating success or failure.

### Update Route Volume

- **Endpoint**: GET `/routes/{route_name}/volume/{volume}`
- **Description**: Sets the volume for a route.
- **Path Parameters**: 
  - `route_name` (string): Name of the route
  - `volume` (float): Volume level (0.0 to 1.0)
- **Response**: Boolean indicating success or failure.

### Update Route Equalizer

- **Endpoint**: POST `/routes/{route_name}/equalizer/`
- **Description**: Sets the equalizer for a route.
- **Path Parameter**: 
  - `route_name` (string): Name of the route
- **Request Body**: `Equalizer` object
- **Response**: Boolean indicating success or failure.

### Update Route Position

- **Endpoint**: GET `/routes/{route_name}/reorder/{new_index}`
- **Description**: Sets the position of a route in the list of routes.
- **Path Parameters**: 
  - `route_name` (string): Name of the route
  - `new_index` (integer): New position in the list
- **Response**: Unspecified (likely a success indicator)

## Streaming

### HTTP MP3 Stream

- **Endpoint**: GET `/stream/{sink_ip}/`
- **Description**: Streams MP3 frames from ScreamRouter.
- **Path Parameter**: 
  - `sink_ip` (string): IP address of the sink
- **Response**: MP3 audio stream

## Site Resources

The API includes several endpoints for serving site resources and HTML dialogs for the web interface. These endpoints are primarily for internal use by the ScreamRouter web application.

## Data Types

### SinkDescription

- `name` (string): Name of the sink
- `ip` (string, nullable): IP address of the sink
- `port` (integer, nullable): Port number (default: 4010)
- `is_group` (boolean): Whether the sink is a group
- `enabled` (boolean): Whether the sink is enabled
- `group_members` (array of strings): Names of group members (for group sinks)
- `volume` (float): Volume level (0.0 to 1.0)
- `bit_depth` (integer): Bit depth (16, 24, or 32)
- `sample_rate` (integer): Sample rate (44100, 48000, 88200, 96000, or 192000)
- `channels` (integer): Number of audio channels (1 to 8)
- `channel_layout` (string): Channel layout type
- `delay` (integer): Delay in milliseconds (0 to 5000)
- `equalizer` (Equalizer object): Equalizer settings
- `time_sync` (boolean): Whether time synchronization is enabled
- `time_sync_delay` (integer): Time synchronization delay

### SourceDescription

- `name` (string): Name of the source
- `ip` (string, nullable): IP address of the source
- `tag` (string, nullable): Tag for the source
- `is_group` (boolean): Whether the source is a group
- `enabled` (boolean): Whether the source is enabled
- `group_members` (array of strings): Names of group members (for group sources)
- `volume` (float): Volume level (0.0 to 1.0)
- `delay` (integer): Delay in milliseconds (0 to 5000)
- `equalizer` (Equalizer object): Equalizer settings
- `vnc_ip` (string): VNC server IP address
- `vnc_port` (integer or string): VNC server port

### RouteDescription

- `name` (string): Name of the route
- `sink` (string): Name of the sink for this route
- `source` (string): Name of the source for this route
- `enabled` (boolean): Whether the route is enabled
- `volume` (float): Volume level (0.0 to 1.0)
- `delay` (integer): Delay in milliseconds (0 to 5000)
- `equalizer` (Equalizer object): Equalizer settings

### Equalizer

An object containing 18 float values (b1 to b18) representing gain for different frequency bands. Each value ranges from 0.0 (full attenuation) to 2.0 (200% volume).

## Limitations

- Volume levels are limited to the range 0.0 to 1.0.
- Delays are limited to the range 0 to 5000 milliseconds.
- Bit depths are limited to 16, 24, or 32 bits.
- Sample rates are limited to specific values (44100, 48000, 88200, 96000, or 192000 Hz).
- The number of audio channels is limited to 1-8.
- Equalizer bands are fixed and cannot be customized beyond the 18 predefined bands.