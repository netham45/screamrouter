#!/usr/bin/make
CC = g++
CFLAGS = -Ofast -fopenmp -std=c++11 -mavx -march=native -mavx -msse4.1
LIBS = -lm

# Find libmp3lame
LIBMP3LAME_PATH := $(shell ldconfig -p | grep libmp3lame.so | awk '{print $$4}' | head -n 1)

ifeq ($(LIBMP3LAME_PATH),)
$(error libmp3lame not found. Please install it and try again.)
endif

Makefile: all

all: makebin libsamplerate bin/sink_audio_mixer bin/source_input_processor bin/rtp_receiver bin/scream_receiver

makebin:
	mkdir -p bin

libsamplerate:
	cd libsamplerate && cmake . && $(MAKE)

bin/sink_audio_mixer: sink_audio_mixer.cpp
	$(CC) $< -o $@ $(LIBS) $(LIBMP3LAME_PATH) $(CFLAGS)

bin/source_input_processor: source_input_processor.cpp biquad/biquad.cpp
	$(CC) $^ -o $@ $(LIBS) libsamplerate/src/libsamplerate.a $(CFLAGS)

bin/rtp_receiver: rtp_receiver.cpp
	$(CC) $< -o $@ $(CFLAGS)

bin/scream_receiver: scream_receiver.cpp
	$(CC) $< -o $@ $(CFLAGS)

clean:
	rm -f bin/sink_audio_mixer bin/source_input_processor bin/rtp_receiver bin/scream_receiver
	rmdir bin
	cd libsamplerate && $(MAKE) clean

.PHONY: all libsamplerate clean
