kill -9 $(ps -ef | grep -iE "scream|ffmpeg|mixer|processor" | awk '{print $2}');./screamrouter.py
