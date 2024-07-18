#!/bin/bash
g++ sink_audio_mixer.cpp -o bin/sink_audio_mixer -lm /usr/lib64/libmp3lame.so -march=native -Ofast -fno-signed-zeros -fno-trapping-math -funroll-loops -fopenmp -D_GLIBCXX_PARALLEL
g++ source_input_processor.cpp biquad/biquad.cpp -o bin/source_input_processor -lm libsamplerate/src/libsamplerate.a -march=native -Ofast -fno-signed-zeros -fno-trapping-math -funroll-loops -D_GLIBCXX_PARALLEL -fopenmp
g++ rtp_receiver.cpp -o bin/rtp_receiver -march=native -Ofast -fno-signed-zeros -fno-trapping-math -funroll-loops -D_GLIBCXX_PARALLEL -fopenmp
g++ scream_receiver.cpp -o bin/scream_receiver -march=native -Ofast -fno-signed-zeros -fno-trapping-math -funroll-loops -D_GLIBCXX_PARALLEL -fopenmp
