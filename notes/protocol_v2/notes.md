# Scream Audio Protocol v2 notes
* RTP Library: ?
* WebRTC Library: libdatachannel (needs added)
* MP3 Library: LibLAME (available already)
* SIP Library: ?

## Audio Out
* Basically shift to RTP while leaving option for old Scream receiver
* RTP can go directly to Pulse/Pipewire, RTP receivers exist for everything
* RTP supports timestamps
* Add WebRTC support for mp3 streams
    * (different port or proxy through host websockets or reverse-proxy host websockets through this?)
* Leave Scream output as-is until all receivers rewritten/replaced
* Split network sender out into a class with scream/rtp/rtc inheriting so it's more modular, currently it's just scream

## Advanced Config page
* Options for sample rate, channels, etc...
* Defaults dropdown with presets for ulaw, etc...

### Protocol Requirements
* Timestamp
* Channel count
* Sample Rate
* Output PCM or MP3 for RTP
* No loss in ability to handle varying incoming audio types
* No loss in channels
* No gain in latency (except due to timestamp sync)
* Include 8-bit audio and lower sample rates in audio processor
* Include endian

## Audio In
* Robust RTP input support
    * Actually decode with a library instead of assuming 16-bit 48kHz, For non-supported PCM go with probably libav for conversion
        * Maybe a thread per session to handle conversion and such? with an inactivity timeout
    * Frequency detection?
* Other than that RTP is largely the same, read in, process to pcm, dump to timeshift queue
* Leave Scream input as-is

## Session Negotiation
* ScreamRouter as SIP presence server
* Move to SIP/SDP
* Identify targets by UUID instead of IP
* Require a pingback every 5s to keep sending data out

## Device Discovery
* Move to Zeroconf with proper srv records for providing the sip presence server basically

### Things to rewrite
#### Add basic RTP header parsing to receivers
* Windows
* esp32
* Linux can use Pulseaudio/Pipewire, write instructions
* Android
* Drop python receiver It hasn't been maintained anyways
* Create a linux app that handles STP + RTP receiving
* Python app? (Are there _any_ python libraries that do 7.1?)

#### Add basic RTP header parsing to senders
* Windows
* esp32
* Linux can use Pulseaudio/Pipewire, write instructions

## Final

* **Finalize a spec that can be applied to all devices**

I want ScreamRouter to be modified to incorporate a SIP presence server that devices can register with. I want the registration flow to be:

Device queries _screamrouter.udp.local over ZeroConf to get the presence server IP/port
Device registers with Presence server by doing SIP negotiation. In this a UUID is included.
At this point if a UUID exists the existing entry is marked online/active and frequency/sample rate/etc... are updated, if it doesn't exist a new Source/SinkDescription will be created for the device and the config reloaded.
During registration the device will indicate if it is a sink or a source

Once registered and active a device will ping the presence server to maintain online status, if it drops offline the presence server will quit sending data

If devices are marked as scream under their sourcedescription/sinkdescription they should be left alone and use the current flow


I believe SIP should be implemented in Python, RTP and WebRTC handling in C++