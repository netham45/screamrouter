#!/usr/bin/make
CC=gcc
CXX = g++
CFLAGS = -Ofast -fopenmp -std=c++14 -mavx -march=native -mavx -msse4.1
LIBS = -lm

# Find libmp3lame
LIBMP3LAME_PATH := $(shell ldconfig -p | grep libmp3lame.so | awk '{print $$4}' | head -n 1)

ifeq ($(LIBMP3LAME_PATH),)
$(error libmp3lame not found. Please install it and try again.)
endif

all: make_bin bin/sink_audio_mixer bin/source_input_processor bin/rtp_receiver bin/scream_receiver

make_bin:
	mkdir -p bin

libsamplerate/src/libsamplerate.a:
	cd libsamplerate && (make || cmake . && $(MAKE))

bin/sink_audio_mixer: sink_audio_mixer.cpp biquad/biquad.cpp audio_processor.cpp speaker_mix.cpp libsamplerate/src/libsamplerate.a
	$(CXX) $^ -o $@ $(LIBS) libsamplerate/src/libsamplerate.a $(LIBMP3LAME_PATH) $(CFLAGS)

bin/source_input_processor: source_input_processor.cpp biquad/biquad.cpp audio_processor.cpp speaker_mix.cpp libsamplerate/src/libsamplerate.a
	$(CXX) $^ -o $@ $(LIBS) libsamplerate/src/libsamplerate.a $(CFLAGS)

bin/rtp_receiver: rtp_receiver.cpp
	$(CXX) $< -o $@ $(CFLAGS)

bin/scream_receiver: scream_receiver.cpp
	$(CXX) $< -o $@ $(CFLAGS)

clean:
	rm -f bin/sink_audio_mixer bin/source_input_processor bin/rtp_receiver bin/scream_receiver
	-$(MAKE) -C libsamplerate clean

.PHONY: all clean make_bin

# Separate phony targets for library builds
.PHONY: build_libsamplerate
build_libsamplerate: libsamplerate/src/libsamplerate.a
