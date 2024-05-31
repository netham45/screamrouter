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
* Uses a custom mixer with no additional functions so latency can be minimized when ffmpeg is not required in the chain
* Can use ffmpeg to delay any sink, route, source, or group so sinks line up better, can use ffmpeg to adjust mismatched sources so they can be mixed
* Can adjust equalization for any sink, route, source, or group
* Contains a plugin system to easily allow additional sources to be added
* Milkdrop Visualizations thanks to the browser-based [Butterchurn](https://github.com/jberg/butterchurn) project
* There are playback sinks available for common OSes such as [Windows](https://github.com/duncanthrax/scream/tree/master/Receivers/dotnet-windows/ScreamReader), [Linux](https://github.com/duncanthrax/scream/tree/master/Receivers/unix), and [Android](https://github.com/martinellimarco/scream-android/tree/90d1364ee36dd12ec9d7d2798926150b370030f3), as well as some embedded devices such as [the ESP32](http://tomeko.net/projects/esp32_rtp_pager/).
* Embedded noVNC for controlling remote computers playing music
* Can accept streams from PulseAudio

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

![Screenshot of HA media player for ScreamRouter Sink](/images/HAMediaPlayer.png)
## Prerequisites

* Scream - https://github.com/duncanthrax/scream
* Configure Scream to use UDP unicast, point it at ScreamRouter on port 16401. The configuration for setting Unicast on the source is in the Scream repo readme.md.
* Install requirements.txt through either pip or your package manager of choice
* Command line ffmpeg
* ScreamRouter is Linux only. It should run in WSL2. Windows compatibility may be reviewed in the future.
* ScreamRouter is configured to use SSL. See src/constants/constants.py to set the path to your certificate. In the future ScreamRouter will be configure to automatically generate a self-signed SSL certificate if none is present.

# Note on installing Scream

Scream has an expired certificate of a type Windows verifies the date of. In order to install it your clock will need to be set earlier than July 6th 2023. Once the clock is set the normal Scream installation instructions, including disabling SecureBoot, will allow it to install on Windows 11. After the driver is installed the clock can be restored to the correct time and the driver will continue to function.

# Usage

## Configuration

ScreamRouter will start up with a blank profile by default. There will be no sources, sinks, or routers configured. You can add them using the web interface.

ScreamRouter's web interface listens on port 443.

Each Sink, Source, and Route has a name. This name is used as the reference for routes and groups to track members. Clicking Add Sink will prompt you for the information to make one. The names must be unique between Sinks and Sink Groups, Sources and Source Groups, and all Routes.

### Latency
ScreamReader will use ffmpeg to convert incompatible streams to the same format, but at the cost of added latency. To avoid using ffmpeg to convert source input:

* Ensure that your Sources and Sink is using the same sample rate, resampling requires ffmpeg.
* Ensure that you are not using an equalizer, the equalizer will require ffmpeg.
* Ensure that you are not using audio delays, they will require ffmpeg.

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

### Source Auto-Discovery

When a source sends to ScreamRouter and it's not already added to the Sources list it will be added with it's IP set as it's name. It will be checked at the time of adding to see if it has VNC available on port 5900 and, if so, VNC will be configured too.


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

The MP3 stream provided from FFMPEG is tracked frame by frame so that ScreamRouter can always start a connection on a new MP3 frame. FFMPEG is configured to generate MP3 with no inter-frame dependencies so files from it can be played back as normal MP3s or streamed to a player starting from any frame.

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


### Processes
This is a multi-processed application using Python Multiprocessing and can take advantage of multiple cores.

The proceses are:

* Receiver process - This receives all data over UDP from sources and puts it in a queue for each sink.
* Audio Controller - One instance for each sink, receives data from the receiver process and passes it to each source input processor for handling
* Source Input Processor - This process adjusts the bit depth to match the sink and applies volume controls. If resampling, equalization, or delays are required it will use ffmpeg for these features, if they are not used ffmpeg is skipped.
* Source Input Processor FFMPEG - This is optional and is only ran if the equalization, sample rate, or delay needs adjusted for a source
* Sink Output Processor - This process mixes the various source inputs together and sends them to the sink. It also passes the stream to ffmpeg so it can be converted to MP3 and streamed to the web interface.
* Sink MP3 Processor FFMPEG  - This runs for each sink and converts PCM audio to MP3 for streaming.
* Sink MP3 Processor - This process reads the MP3 output of FFMPEG in and divides it into MP3 frames. It sends the data to the WebStream queue that FastAPI watches when a stream request is made.
* API/Main process - FastAPI runs in it's own process and will send requests in their own threads. When the MP3 Stream endpoint is called it will delay in an async function to wait for the WebStream queue to have data and send data when available.

### More Technical Info
Each Sink is associated with a Sink Controller.

On the reciving process receiving a packet it is forwarded to a queue for each Sink Controller. Each Sink controller will wait for the queue and check if the data matches a source it tracks. If so they send the data to the appropriate source for processing.

Each source will process data to ensure the bit depth and samplerate match. Bit depth and volume are corrected by the low latency codepath, equalization, mismatched sample rates, and adding a static delay go through the ffmpeg codepath.

After the source has processed the data it is sent to the sink mixing thread. This thread mixes all the sources into one output and sends it to a Scream receiver and to ffmpeg to be converted to an MP3 stream for the web interface.

## Troubleshooting

Here are some problems and solutions:

* Problem: Audio frequently makes a loud static noise
* Solution: Change your sources to either 16 or 32-bit depth, avoid using 24-bit.

* Problem: There are crackles and pops in the audio
* Solution: Try to keep the source and destinations configured the same. I've had best luck with 48kHz 32-bit.

* Problem: The audio is choppy when I use high channel setups such as 7.1 32-bit
* Solution: Turn down your bit depth. 8 channels at 32-bit 48000kHz is over 12Mb/s, this is a lot for real-time audio. 16-bit will halve your bitrate. Consider 5.1 or Stereo.

* Problem: My Raspberry Pi keeps cutting out during streaming
* Solution: Consider a better USB wifi card for the Raspberry Pi. They have weak wifi reception. Consider setting the sink to the lowest possible bitrate, too.
