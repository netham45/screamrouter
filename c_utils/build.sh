#!/bin/bash
g++ -O3 sink_audio_mixer.cpp -o bin/sink_audio_mixer
g++ -O3 source_input_processor.cpp biquad/biquad.cpp -o bin/source_input_processor -lm libsamplerate/src/libsamplerate.a
