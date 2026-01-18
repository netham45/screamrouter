#!/bin/bash
# script for testing, shouldn't be needed for normal use
(kill $(ps -ef | grep callgrind | awk '{print $2}'); sleep .5
kill -9 $(ps -ef | grep -iE "scream|ffmpeg|mixer|processor" | grep -vE "$0|antigravity" | awk '{print $2}'))&>/dev/null
if [[ "$1" == "restart" ]]
then
#    rm -f *.so
    RTCDC_LOG=trace JUICE_DEBUG=1 LIBNICE_DEBUG=all SCREAMROUTER_TRACE=1 SCREAMROUTER_TRACE_FILE=/tmp/sr.json NPM_REACT_DEBUG_SITE=true python3 -m screamrouter
fi
