#!/bin/bash
# script for testing, shouldn't be needed for normal use
(kill $(ps -ef | grep callgrind | awk '{print $2}'); sleep .5
kill -9 $(ps -ef | grep -iE "scream|ffmpeg|mixer|processor" | grep -v "$0" | awk '{print $2}'))&>/dev/null
if [[ "$1" == "restart" ]]
then
    SCREAMROUTER_TRACE=1 SCREAMROUTER_TRACE_FILE=/tmp/sr.json NPM_REACT_DEBUG_SITE=true python3 screamrouter
fi
