kill -9 $(ps -ef | grep -iE "scream|ffmpeg" | awk '{print $2}');./screamrouter.py
