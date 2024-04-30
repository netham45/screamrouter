# **ScreamRouter**

![Screenshot of ScreamRouter](/images/ScreamRouter.png)

### What is it
ScreamRouter is a Python-based audio router for Scream sources and sinks. It allows you to enter an IP address for all of your Scream audio sources and IP address, port, bit depth, sample rate, and channel configuration for all of your sinks. It has a web interface for managing the configuration and listening to sinks.

### Features
* Configure Sources based off of IP address, Configure Routes between Sources and Sinks, Configure Sinks with Bit Depth, Sample Rate, Channel Layout, Sink IP, and Port
* Group together Sources and Sinks to control and play to multiple at a time
* Control the volume for each Source, Route, Sink, and Group the stream passes through
* Exposes an MP3 stream of all sinks so they can be listened to in a browser
* Can play a URL out of a sink or sink group
* Has a Home Assistant Custom Component for managing Sinks and playing media back through Sinks (See: https://github.com/netham45/screamrouter_ha_component )
* Automatically saves to YAML on setting change
* Uses ffmpeg to mix sources together into final sink stream to be played to the Scream sink
* Can use ffmpeg to delay sinks so sinks line up better
* Can adjust equalization for any sink

![Screenshot of ScreamRouter Equalizer](/images/Equalizer.png)

### Use Cases
* Mixing one or many Scream Sources to one or many Scream Receivers for a whole-house audio setup
* Changing the volume of groups of sinks at once while having each sink also individually leveled
* Use a web browser as a sink via the 'Listen to Sink' feature
* Use any streaming MP3 player as a sink via the exposed API
* Programatically enable/disable sinks, or adjust the volume through the FastAPI API, or through Home Assistant
* Play back sound effects and Text to Speech to arbitrary Sinks from Home Assistant automations
* Line up speakers with differing timings by delaying one
* Adjusting EQ to a cheap speaker so it sounds better

![Screenshot of HA media player for ScreamRouter Sink](/images/HAMediaPlayer.png)
## Prerequisites

* Scream - https://github.com/duncanthrax/scream
* Configure Scream to use UDP unicast, point it at ScreamRouter on port 16401. The configuration for setting Unicast is in the Scream repo readme.md.
* Install requirements.txt through either pip or your package manager of choice
* Command line ffmpeg

# Note on installing Scream

Scream has an expired certificate of a type Windows verifies the date of. In order to install it your clock will need to be set earlier than July 6th 2023. Once the clock is set the normal Scream installation instructions, including disabling SecureBoot, will allow it to install on Windows 11. After the driver is installed the clock can be restored to the correct time and the driver will continue to function.

# Usage

## Configuration

ScreamRouter will start up with a blank profile by default. There will be no sources, sinks, or routers configured. You can add them by editing the yaml or by using the interface.

ScreamRouter's web interface listens on port 8080.

The interface will update the yaml so any notes, non-standard fields, or custom layouts in the yaml will be lost. The interface will prompt you for the required information when you go to add an entry.

Each Sink, Source, and Route has a name. This name is used as the reference for routes and groups to track members. Clicking Add Sink will prompt you for the information to make one. The names must be unique between Sinks and Sink Groups, Sources and Source Groups, and all Routes.

### Sinks
Each Sink holds information for the destination IP, port, volume, sample rate, bit depth, channels, channel layout, and the sink name, and how many ms it is delayed

![Screenshot of Add Sink Dialog](/images/AddSink.png)

Example YAML block:

This defines two sinks, Livingroom and Bedroom.

```
sinks:
- enabled: true
  ip: 192.168.3.178
  name: Livingroom
  port: 4010
  volume: 1.0
  bitdepth: 32
  channel_layout: stereo
  channels: 2
  samplerate: 48000
  delay: 0
  equalizer:
    b1: 1.0
    b10: 1.0
    b11: 1.0
    b12: 1.0
    b13: 1.0
    b14: 1.0
    b15: 1.0
    b16: 1.0
    b17: 1.0
    b18: 1.0
    b2: 1.0
    b3: 1.0
    b4: 1.0
    b5: 1.0
    b6: 1.0
    b7: 1.0
    b8: 1.0
    b9: 1.0
- enabled: true
  ip: 192.168.3.111
  name: Bedroom
  port: 4010
  volume: 1.0
  bitdepth: 32
  channel_layout: stereo
  channels: 2
  samplerate: 48000
  delay: 0
  equalizer:
    b1: 1.0
    b10: 1.0
    b11: 1.0
    b12: 1.0
    b13: 1.0
    b14: 1.0
    b15: 1.0
    b16: 1.0
    b17: 1.0
    b18: 1.0
    b2: 1.0
    b3: 1.0
    b4: 1.0
    b5: 1.0
    b6: 1.0
    b7: 1.0
    b8: 1.0
    b9: 1.0
```

### Routes
Each Route holds one Sink name and one Source name, and volume.

![Screenshot of Add Route Dialog](/images/AddRoute.png)

Example YAML block:

This defines two routes, Music to All and Office to Office PC.

```
routes:
- enabled: true
  name: Music to All
  sink: All
  source: Music
  volume: 1.0
- enabled: true
  name: Office
  sink: Office
  source: Office PC
  volume: 1.0
```

### Sources
Each Source holds information for the source IP, volume, and the Source name.

![Screenshot of Add Source Dialog](/images/AddSource.png)

Example YAML block:

This defines two sources, Server and Livingroom PC

```
sources:
- enabled: true
  ip: 192.168.3.119
  name: Server
  volume: 1.0
- enabled: true
  ip: 192.168.3.172
  name: Livingroom PC
  volume: 1.0
```


### Sink Groups
Each Sink Group holds a name, a list of Sinks, and a volume. Groups can be nested.

### Source Groups

Each Source Group holds a name, a list of Sources, and a volume. Groups can be nested.

Example YAML block:

This defines two sink groups and one Source group. The sinks are 'All' containing 'Livingroom', 'Bedroom', and 'Office', and 'Bedroom Group' containing 'Bedroom' and 'Bathroom'. The source group is 'Music', containing just 'Server'.

```
groups:
  sinks:
  - enabled: true
    name: All
    sinks:
    - Livingroom
    - Bedroom
    - Office
    volume: 1.0
  - enabled: true
    name: Bedroom Group
    sinks:
    - Bedroom
    - Bathroom
    volume: 1.0
  sources:
  - enabled: true
    name: Music
    sources:
    - Server
    volume: 1.0
```

### Server Configuration

The server configuration holds the port for the API to listen on and the port for the recevier to receive audio frames on. These options can only be edited in the YAML.

Example YAML block:

* api_port: Port for the ScreamRouter API to listen to (Default 8080)
* logs_dir: Directory to log to (Default logs/)
* receiver_port: UDP port for ScreamRouter to listen for sources to send to (Dfeault: 16401)
* pipes_dir: Directory to store temproary pipes in (Default pipes/)
* console_log_level: Log level (Default info, Valid log levels: critical=no logs, error, warn, info, debug)

```
server:
  api_port: 8080
  logs_dir: logs/
  receiver_port: 16401
  pipes_dir: pipes/
  console_log_level: info
```

## General Info

The basic route for ScreamRouter is:

Scream on Source PC -> ScreamRouter Source -> ScreamRouter Route -> ScreamRouter Sink -> Scream Receiver on Sink PC

Each ScreamRouter Source is an IP address it looks for data from.

Each ScreamRouter Sink is an IP address and port it sends data to, along with a configuration for the output stream properties (bit depth, sample rate, channels, channel layout)

The sinks can take in any input format Scream can produce, but using 24-bit is discouraged due to potential byte alignment issues. If you get random static blasted at you during playback this is why.

Each ScreamRouter Route is a link of one Source to one Sink, and each Sink is an IP and Port for ScreamRouter to unicast data to. ScreamRouter receives traffic from all sources on one UDP port and filters based off of the source IP.

### Volume
Each Source, Route, and Sink has a volume control. The default volume of 1 is unattenuated. The volumes for each sink, sink group, route, source group, and source it passes through are multiplied together to come up with a final volume.

### Delay
Delays will add gaps to streams when sources come in and drop out. To avoid this set delays to 0.

## Technical Info

### API
ScreamRouter uses FastAPI. The API documentation is enabled and can be viewed by accessing `http://<Your ScreamRouter Server>:8080/docs` . This is the REST API the interface uses that allows adding and removing Sources, Routes, Sinks, along with Source and Sink Groups.

### MP3 Stream
It also exposes an MP3 stream of each sink. This is available at both `http://<Your ScreamRouter Server>:8080/stream/<IP of sink>/` and `ws://<Your ScreamRouter Server>:8080/ws/<IP of sink>/` (Note the trailing slashes)

The MP3 stream provided from FFMPEG is tracked frame by frame so that ScreamRouter can always start a connection on a new MP3 frame. FFMPEG is configured to generate MP3 with no inter-frame dependencies so files from it can be played back as normal MP3s or streamed to a player starting from any frame.

The latency for streaming the MP3s to VLC is low, browsers will enforce a few seconds of caching. Mobile browsers sometimes more. Some clients refuse to play MP3s received one frame at a time, an option will be added to the configuration to adjust the number of frames buffered for MP3 data.

### Threads
This is a multi-threaded application and can take advantage of multiple cores.

The threads are:

* Receiver thread - This receives all data over UDP from sources and puts it in a queue for each sink.
* Sink Queue thread - This thread watches the sink queue and fires a callback that sends any incoming data to the FFMPEG pipes.
* Sink MP3 input thread - This thread watches the MP3 pipe output of FFMPEG and reads the data in. It sends the data to the WebStream queue that FastAPI watches when a stream request is made.
* Sink PCM input thread - This thread watches the PCM pipe output of FFMPEG and reads the data in. It sends the data to each enabled Scream Sink.
* Sink FFMPEG thread - This thread handles starting and stopping FFMPEG as well as ensuring the process is running when it should be. The class also handles building the command line for FFMPEG.
* API thread - FastAPI runs in it's own thread and will send requests in their own threads. When the Stream endpoint is called it will delay in an async function to wait for the WebStream queue to have data.

### More Technical Info
Each Sink is associated with a Sink Controller. This controller reads into and out of the FIFO pipes for ffmpeg and tracks which sources are assigned and active.

On the reciving thread receiving a packet it is forwarded to a queue for each Sink Controller. Each Sink controller will wait for the queue and check if the data matches a source it tracks. If so they send the data to the appropriate input in FFMPEG over a pipe.

The Sink Controller also manages two threads to read input from FFMPEG, both PCM and MP3. The PCM thread is sent to Scream Receiver Sources, the MP3 stream is forwarded to a queue handler to make it available to FastAPI.


The lifecycle of each thread and ffmpeg process is controlled from start to end by ScreamRouter so that a configuration change can fully unload and reload ScreamRouter.
