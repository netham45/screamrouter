# **ScreamRouter**

![Screenshot of ScreamRouter](/images/ScreamRouter.png)

### What is it
ScreamRouter is a Python-based audio router for Scream sources and sinks. It allows you to enter an IP address for all of your Scream audio sources and IP address, port, bit depth, sample rate, and channel configuration for all of your sinks. It has a web interface for managing the configuration and listening to sinks.

### What is Scream
[Scream is a virtual audio driver for Windows](https://github.com/duncanthrax/scream) created by Tom Kistner. It operates by sending any received PCM data from Windows's sound engine over the network unprocessed. This allows very low latency. Each packet from Scream is 1152 bytes worth of PCM from Windows's audio engine with a 5 byte header that describes the format of the stream.

The simplicity of Scream allows it to be very easy to work with while retaining low latency.

### Features
* Configure Sources based off of IP address, Configure Routes between Sources and Sinks, Configure Sinks with Bit Depth, Sample Rate, Channel Layout, Sink IP, and Port
* Group together Sources and Sinks to control and play to multiple at a time
* Control the volume for each Source, Route, Sink, and Group the stream passes through
* Exposes an MP3 stream of all sinks so they can be listened to in a browser
* Can play a URL out of a sink or sink group
* Has a Home Assistant Custom Component for managing Sinks and playing media back through Sinks (See: https://github.com/netham45/screamrouter_ha_component )
* Automatically saves to YAML on setting change
* Uses a custom mixer/equalizer/channel layout up/downmixer so latency can be minimized
* Can adjust equalization for any sink, route, source, or group
* Contains a plugin system to easily allow additional sources to be added
* Milkdrop Visualizations thanks to the browser-based [Butterchurn](https://github.com/jberg/butterchurn) project
* There are playback sinks available for common OSes such as [Windows](https://github.com/duncanthrax/scream/tree/master/Receivers/dotnet-windows/ScreamReader), [Linux](https://github.com/duncanthrax/scream/tree/master/Receivers/unix), and [Android](https://github.com/martinellimarco/scream-android/tree/90d1364ee36dd12ec9d7d2798926150b370030f3). I've written one for [ESP32 A1S Audiokit Dev boards](https://github.com/netham45/esp32-audiokit-screamreader/), and [one for esp32/esp32s that does toslink and usb uac 1.0 output](https://github.com/netham45/esp32-scream-receiver/) and I've written a multi-platform receiver in [Python](https://github.com/netham45/pyscreamreader).
* Embedded noVNC for controlling remote computers playing music
* Can accept streams from PulseAudio
* Docker containers to add support for [Amazon Music](https://github.com/netham45/screamrouter-amazon-music-docker), [Firefox](https://github.com/netham45/screamrouter-firefox-docker), [Spotify](https://github.com/netham45/screamrouter-spotify-docker)
* Linux tool for sending PCM data from the command line [ScreamSender](https://github.com/netham45/screamsender/)
* Ability to send media control commands to containers from API and web interface

![Screenshot of ScreamRouter noVNC](/images/noVNC.png)

### Use Cases
* Mixing one or many Scream Sources to one or many Scream Receivers for a whole-house audio setup
* Changing the volume of groups of sinks at once while having each sink also individually leveled
* Use a web browser as a sink via the 'Listen to Sink' feature
* Use any streaming MP3 player as a sink via the exposed API
* Programatically enable/disable sinks, or adjust the volume through the FastAPI API, or through Home Assistant
* Play back sound effects and Text to Speech to arbitrary Sinks from Home Assistant automations
* Line up speakers with differing timings by delaying one
* Adjusting EQ for a cheap speaker so it sounds better

![Screenshot of HA for ScreamRouter](/images/HAMediaPlayer.png)
## Prerequisites

* Scream - https://github.com/duncanthrax/scream
* Configure Scream to use UDP unicast, point it at ScreamRouter on port 16401. The configuration for setting Unicast on the source is in the Scream repo readme.md.
* Install requirements.txt through either pip or your package manager of choice
* ScreamRouter is Linux only. It should run in WSL2. Windows compatibility may be reviewed in the future.
* ScreamRouter is configured to use SSL. See src/constants/constants.py to set the path to your certificate. In the future ScreamRouter will be configure to automatically generate a self-signed SSL certificate if none is present.

# Note on installing Scream

Scream has an expired certificate of a type Windows verifies the date of. In order to install it your clock will need to be set earlier than July 6th 2023. Once the clock is set the normal Scream installation instructions, including disabling SecureBoot, will allow it to install on Windows 11. After the driver is installed the clock can be restored to the correct time and the driver will continue to function.

# Usage

## Configuration

ScreamRouter will start up with a blank profile by default. There will be no sources, sinks, or routers configured. You can add them using the web interface.

ScreamRouter's web interface listens on port 443.

Each Sink, Source, and Route has a name. This name is used as the reference for routes and groups to track members. Clicking Add Sink will prompt you for the information to make one. The names must be unique between Sinks and Sink Groups, Sources and Source Groups, and all Routes.

### Sinks
Each Sink holds information for the destination IP, port, volume, sample rate, bit depth, channels, channel layout, and the sink name, and how many ms it is delayed

![Screenshot of Add Sink Dialog](/images/AddSink.png)

### Routes
Each Route holds one Sink name and one Source name, and volume.

![Screenshot of Add Route Dialog](/images/AddRoute.png)


### Sources
Each Source holds information for the source IP, volume, and the Source name.

![Screenshot of Add Source Dialog](/images/AddSource.png)


### PulseAudio

PulseAudio can send to ScreamRouter over RTP. The RTP sender needs a set MTU size to match Scream's packet size. The following command will send the Dummy source to a ScreamRouter Receiver:

    pactl load-module module-rtp-send format=s16le source=auto_null.monitor destination=<ScreamRouter Receiver> port=40000 mtu=1164

Currently the RTP receiver assumes 16-bit 44100kHz audio with 1152 bytes of PCM in the payload.

### Source Auto-Detection

When a source sends to ScreamRouter and it's not already added to the Sources list it will be added with it's hostname set as it's name if it can be determined, else it's IP. It will be checked at the time of adding to see if it has VNC available on port 5900 and, if so, VNC will be configured too.

### Sink Groups
Each Sink Group holds a name, a list of Sinks, and a volume. Groups can be nested.

### Source Groups

Each Source Group holds a name, a list of Sources, and a volume. Groups can be nested.

### Server Configuration

The server configuration holds the port for the API to listen on and the port for the recevier to receive audio frames on. You can currently find it at src/constants/constants.py .

These are the options in constants.py:

* `RECEIVER_PORT` `Default: 16401` This is the port for the receiver
* `SINK_PORT` `Default: 4010` This is the port for a Scream Sink
* `API_PORT` `Default: 8080` This is the port FastAPI runs on
* `API_HOST` `Default: "0.0.0.0"` This is the host FastAPI binds to
* `MP3_STREAM_BITRATE` `Default: "320k"` MP3 stream bitrate for the web API
* `MP3_STREAM_SAMPLERATE` `Default: 48000` MP3 stream sample for the web API
* `LOGS_DIR` `Default: "./logs/"` This is the directory logs are stored in
* `CONSOLE_LOG_LEVEL` `Default: "INFO"` Log level for stdout, valid values are `"DEBUG"`, `"INFO"`, `"WARNING"`, `"ERROR"`
* `LOG_TO_FILE` `Default: True` Determines rather logs are written to files
* `CLEAR_LOGS_ON_RESTART` `Default: True` Determines rather logs are cleared on restart
* `DEBUG_MULTIPROCESSING`  `Default: False` Debugs Multiprocessing to stdout.
* `SHOW_FFMPEG_OUTPUT` `Default: False` Show ffmpeg output to stdout.
* `SOURCE_INACTIVE_TIME_MS` `Default: 150` Inactive time for a source before it's closed. Some plugins may need this raised. If this is too long there will be gaps when a source stops sending. Having the value as short as possible without false positives will perform the best.
* `CERTIFICATE` Needs set to the path to your SSL certificate
* `CERTIFICATE_KEY` Needs set to the path to your SSL certificate private key

### YAML

The YAML is generated from Pydantic. It can be manually edited but it is messy and not user-friendly. Take a backup before you make any changes. It is verified on load and will error out if the configuration is invalid.

## General Info

The basic route for ScreamRouter is:

Scream on Source PC -> ScreamRouter Source -> ScreamRouter Route -> ScreamRouter Sink -> Scream Receiver on Sink PC

Each ScreamRouter Source is an IP address it looks for data from.

Each ScreamRouter Sink is an IP address and port it sends data to, along with a configuration for the output stream properties (bit depth, sample rate, channels, channel layout)

The sinks can take in any input format Scream can produce, but using 24-bit is discouraged due to potential byte alignment issues. If you get random static blasted at you during playback this is why.

Each ScreamRouter Route is a link of one Source to one Sink, and each Sink is an IP and Port for ScreamRouter to unicast data to. ScreamRouter receives traffic from all sources on one UDP port and filters based off of the source IP.

### Volume
Each Source, Route, and Sink has a volume control. The default volume of 1 is unattenuated. The volumes for each sink, sink group, route, source group, and source it passes through are multiplied together to come up with a final volume.

### Equalizer
Much like the volume, Source, Route, and Sink has an equalizer. The default equalizer is 1, it has a minimum of 0 and a maximum of 2. The equalizations for each sink, sink group, route, source group, and source it passes through are multiplied together per band to come up with a final volume.

![Screenshot of ScreamRouter Equalizer](/images/Equalizer.png)

### Delay
Delays will add gaps to streams when sources come in and drop out. To avoid this set delays to 0.

## Technical Info

### API
ScreamRouter uses FastAPI. The API documentation is enabled and can be viewed by accessing `http://<Your ScreamRouter Server>:8080/docs` . This is the REST API the interface uses that allows adding and removing Sources, Routes, Sinks, along with Source and Sink Groups.

### MP3 Stream
It also exposes an MP3 stream of each sink. This is available at both `http://<Your ScreamRouter Server>:8080/stream/<IP of sink>/` and `ws://<Your ScreamRouter Server>:8080/ws/<IP of sink>/` (Note the trailing slashes)

The MP3 stream provided from liblame is tracked frame by frame so that ScreamRouter can always start a connection on a new MP3 frame.

The latency for streaming the MP3s to VLC is low, browsers will enforce a few seconds of caching. Mobile browsers sometimes more. Some clients refuse to play MP3s received one frame at a time, an option will be added to the configuration to adjust the number of frames buffered for MP3 data.


### Plugins
ScreamRouter supports Source Input Plugins. These plugins can be anything that outputs PCM. There is currently one plugin, PlayURL. It can be viewed as an example layout of plugins: https://github.com/netham45/screamrouter/blob/main/src/plugins/play_url.py

Each plugin is a Multiprocessing thread that is provided a queue and functions to add/remove temporary and permanent sources.

Notable functions are:

    add_temporary_source(sink_name: SinkNameType, source: SourceDescription)

    This adds a temporary source to a sink or sink group in ScreamRouter. They are 
    not saved and will not persist across reloads.

    remove_temporary_source(sink_name: SinkNameType)

This removes a temporary source when it is done playing back

    add_permanet_source(self, source: SourceDescription)

Adds a permanent source that shows up in the API and gets saved
If your plugin tag changes the source will break.
Overwrites existing sources with the same name.
Permanent sources can be removed from the UI or API once they're no longer
in use.

    def create_stream_info(bit_depth: BitDepthType,
                           sample_rate: SampleRateType,
                           channels: ChannelsType,
                           channel_layout: ChannelLayoutType) -> ScreamHeader```

This function lives under src.audio.scream_header_parser and handles generating
  Scream headers.


In the plugin the following functions are called during the plugin lifecycle:
    
    
    def start_plugin(self)
    
This is called when the plugin is started. API endpoints should be added here. Any other startup tasks can be performed too.

    def stop_plugin(self)

This is called when the plugin is stopped or ScreamRouter unloaded. You may perform shutdown tasks here.

    def load_plugin(self)

This is called when the configuration is (re)loaded.

    def unload_plugin(self)

This is called when the configuration is unloaded.

The loader is not fully implemented yet but will load Python stored in src/plugins as plugins.

### TCP Client
TCP client support was added. A client can connect to TCP on the port configured in constants.py to receive PCM data destined for that Sink Source. This will stop UDP output on the port. Currently stream parameters are not sent over TCP.

### Media Controls
ScreamRouter can send Play/Next/Previous commands to sources. They will be sent as UDP packetscontaining 'n', 'p', or 'P' for next track, previous track, and play/pause respectively. They will be sent to port 9999 on the source. These are currently listened to by a bash script in the media containers.

They will be visible in the UI as long as a VNC host is configured.


### Processes
This is a multi-processed application using Python Multiprocessing and can take advantage of multiple cores.

The proceses are:

* Receiver processes - These receives all data over UDP from sources and puts it in a queue for each sink.
* Source Input Processor - This process adjusts the bit depth to match the sink and applies volume controls. This is a C++ process.
* Sink Output Processor - This process mixes the various source inputs together and sends them to the sink, as well as generating an MP3 stream to send to the browser. This is a C++ process.
* Sink MP3 Processor - This process reads the MP3 output from the sink output processor in and divides it into individual MP3 frames. It sends the data to the WebStream queue that FastAPI watches when a stream request is made.
* API/Main process - FastAPI runs in it's own process and will send requests in their own threads. When the MP3 Stream endpoint is called it will delay in an async function to wait for the WebStream queue to have data and send data when available.

### More Technical Info
Data comes in through the RTC and Scream recevers. It is duplicated there and sent to FDs leading to each Source Input Processor. Each Source Input Processor verifies the IP is what it's programmed to care about and if so processes the packet (equalization, delay, volume, resampling, channel layout) and sends it on to the Sink Output Processor. The Sink Output Process will handle mixing the converted streams and downscaling to the desired output bitdepth, then send the data to Scream/TCP sinks. It will also convert it to an MP3 via lame and send it to the Sink MP3 Processor. The Sink MP3 Processor will then divide that stream into individual MP3 frames, which are sent to the WebStream queue to be sent to MP3 clients.

# Install
These instructions are not thorough.

* Clone the repo
* Install dependencies TODO: List Python and C++ dependencies
* Build C++ backend `cd c_utils;./build`
* Put SSL certs in certs/
* Verify configuration in src/constants/constants.py
* Run the Python server `./screamrouter.py`