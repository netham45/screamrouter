#!/usr/bin/env bash
set -euo pipefail

PREFIX=${PREFIX:-/usr}
LIBDIR=${LIBDIR:-$PREFIX/lib}
SHAREDIR=${SHAREDIR:-$PREFIX/share/alsa/alsa.conf.d}
RUNTIME_BASE=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}
RUNTIME_BASE=${RUNTIME_BASE%/}
DEFAULT_RUNDIR=${RUNTIME_BASE:+$RUNTIME_BASE/screamrouter}
DEFAULT_RUNDIR=${DEFAULT_RUNDIR:-/var/run/screamrouter}
RUNDIR=${RUNDIR:-$DEFAULT_RUNDIR}
SOUND_GROUP=${SOUND_GROUP:-audio}
PULSE_DEFAULT_PAD_DIR=${PULSE_DEFAULT_PAD_DIR:-/etc/pulse/default.pa.d}

if [[ ! -f libasound_module_pcm_screamrouter.so ]]; then
  echo "Build the plugin first (run make)." >&2
  exit 1
fi

install -d "$LIBDIR/alsa-lib"
install -m 0755 libasound_module_pcm_screamrouter.so "$LIBDIR/alsa-lib/"

install -d "$SHAREDIR"
cat <<'CONF' > "$SHAREDIR/50-screamrouter.conf"
pcm_type.screamrouter {
    lib "libasound_module_pcm_screamrouter.so"
    open "_snd_pcm_screamrouter_open"
}
pcm.screamrouter {
    @args [ DEVICE ]
    @args.DEVICE {
        type string
        default ""
    }
    type screamrouter
    device $DEVICE
}
pcm.screamrouter_default {
    type screamrouter
    device "default"
}
pcm.!default {
    type plug
    slave.pcm "screamrouter_default"
}
CONF

install -d "$PULSE_DEFAULT_PAD_DIR"
cat <<'PA' > "$PULSE_DEFAULT_PAD_DIR/50-screamrouter.pa"
load-module module-alsa-sink device=screamrouter sink_name=screamrouter tsched=0
load-module module-alsa-source device=screamrouter source_name=screamrouter_in tsched=0
PA

install -d -m 2770 "$RUNDIR"
if getent group "$SOUND_GROUP" >/dev/null 2>&1; then
  chgrp "$SOUND_GROUP" "$RUNDIR"
fi

printf 'Installed screamrouter plugin to %s\n' "$LIBDIR/alsa-lib"
printf 'Configuration drop-in written to %s\n' "$SHAREDIR/50-screamrouter.conf"
printf 'Runtime directory ensured at %s\n' "$RUNDIR"
printf 'PulseAudio drop-in written to %s\n' "$PULSE_DEFAULT_PAD_DIR/50-screamrouter.pa"
