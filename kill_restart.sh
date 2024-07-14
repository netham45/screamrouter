#!/bin/bash
kill -9 $(ps -ef | grep -iE "scream|ffmpeg|mixer|processor" | awk '{print $2}')
if [[ "$1" == "restart" ]]
then
	./screamrouter.py
fi
