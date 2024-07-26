#!/bin/bash
mkdir -p bin
optimize="-Ofast -fopenmp -std=c++11 -mavx -march=native -mavx -msse4.1"
g++ sink_audio_mixer.cpp -o bin/sink_audio_mixer -lm /usr/lib64/libmp3lame.so $optimize
g++ source_input_processor.cpp biquad/biquad.cpp -o bin/source_input_processor -lm libsamplerate/src/libsamplerate.a $optimize
g++ rtp_receiver.cpp -o bin/rtp_receiver $optimize
g++ scream_receiver.cpp -o bin/scream_receiver $optimize
