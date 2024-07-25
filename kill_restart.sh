#!/bin/bash
# script for testing, shouldn't be needed for normal use
kill -9 $(ps -ef | grep -iE "scream|ffmpeg|mixer|processor" | grep -v "$0" | awk '{print $2}')
if [[ "$1" == "restart" ]]
then
	./screamrouter.py
fi
