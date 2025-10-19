# Screamrouter ALSA Plugin

This directory hosts a custom ALSA PCM plugin that exposes screamrouter
loopback taps by dynamically creating FIFOs under `/var/run/screamrouter`
whenever an application opens `screamrouter:<name>`.

The plugin:
- Registers a single `pcm_type.screamrouter` loader stub and a bootstrap
  `pcm.screamrouter` definition.
- When an application opens `screamrouter:<name>` for playback, the plugin
  creates (if necessary) a FIFO named
  `/var/run/screamrouter/out.<label>.<rate>Hz.<channels>ch.<bits>bit.<format>`
  and mirrors the playback stream into it.
- When opened for capture, the plugin creates
  `/var/run/screamrouter/in.<label>.<rate>Hz.<channels>ch.<bits>bit.<format>`
  and feeds captured audio back to the application from that FIFO.
- Uses the ALSA IO-plug SDK to present the FIFO-backed stream to
  applications while decoupling playback from FIFO back-pressure.

Build the shared library with `make` (requires `alsa-lib` development
headers) and install it with `make install` (honours `PREFIX`, `DESTDIR`,
`DEVICE_DIR`, and `SOUND_GROUP`) to drop the module into ALSA’s plugin
path, provision `/var/run/screamrouter/`, and write the bootstrap config
snippet.

`make install` provisions `/var/run/screamrouter/`, installs the shared
library, and writes the config snippet under `/etc/alsa/conf.d/`.

### Using the PCM

After `make install`, ALSA exposes a single logical PCM named
`screamrouter`. Open a specific tap by appending its dynamic name after a
colon, for example:

    aplay -D screamrouter:monitor sample.wav
    arecord -D screamrouter:music -f cd out.wav

Read/write the corresponding FIFOs under `/var/run/screamrouter/` (for
example `cat /var/run/screamrouter/out.monitor > capture.raw`).

`<label>` is a lower-case, filesystem-safe version of `<name>`.
FIFOs are created on first open and automatically removed again when the
stream closes.
Ensure `/var/run/screamrouter` exists (owned by root:audio, mode `2770`).
