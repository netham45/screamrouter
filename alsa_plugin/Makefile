PREFIX ?= /usr
DESTDIR ?=
ALSA_LIBDIR ?= $(shell pkg-config --variable=libdir alsa 2>/dev/null)
ALSA_LIBDIR ?= $(PREFIX)/lib
PLUGIN_DIR ?= $(ALSA_LIBDIR)/alsa-lib
CONF_DIR ?= /etc/alsa/conf.d
DEVICE_DIR ?= /var/run/screamrouter
SOUND_GROUP ?= audio

ALSA_INCLUDE ?= $(shell pkg-config --cflags alsa)
ALSA_LIBS ?= $(shell pkg-config --libs alsa)

SRC = src/pcm_screamrouter.c
OBJ = $(SRC:.c=.o)

CFLAGS ?= -O2 -fPIC -DPIC -Wall -Wextra -Wformat -Werror=return-type
CFLAGS += $(ALSA_INCLUDE)
LDFLAGS += -shared

TARGET = libasound_module_pcm_screamrouter.so

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(ALSA_LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: $(TARGET)
	install -d -m 2770 $(DESTDIR)$(DEVICE_DIR)
	@if getent group $(SOUND_GROUP) >/dev/null 2>&1; then \
		chgrp $(SOUND_GROUP) $(DESTDIR)$(DEVICE_DIR); \
	fi
	install -d $(DESTDIR)$(PLUGIN_DIR)
	install -m 0755 $(TARGET) $(DESTDIR)$(PLUGIN_DIR)/
	install -d $(DESTDIR)$(CONF_DIR)
	printf '%s\n' \
	  'pcm_type.screamrouter {' \
	  '    lib "libasound_module_pcm_screamrouter.so"' \
	  '    open "_snd_pcm_screamrouter_open"' \
	  '}' \
	  'pcm.screamrouter {' \
	  '    @args [ DEVICE ]' \
	  '    @args.DEVICE {' \
	  '        type string' \
	  '        default ""' \
	  '    }' \
	  '    type screamrouter' \
	  '    device $$DEVICE' \
	  '}' \
	  > $(DESTDIR)$(CONF_DIR)/50-screamrouter.conf

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean install
