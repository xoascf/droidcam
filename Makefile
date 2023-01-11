# DroidCam & DroidCamX (C) 2010-2021
# https://github.com/dev47apps
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# Use at your own risk. See README file for more details.

# Variables with ?= can be changed during invocation
# Example:
#  APPINDICATOR=ayatana-appindicator3-0.1 make droidcam

APPINDICATOR ?= appindicator3-0.1
CFLAGS ?= -Wall -O2

GTK   = `pkg-config --libs --cflags gtk+-3.0` `pkg-config --libs x11`
GTK  += `pkg-config --libs --cflags $(APPINDICATOR)`
LIBAV = `pkg-config --libs --cflags libswscale libavutil`
LIBS  = -lspeex -lasound -lpthread -lm
JPEG  = `pkg-config --libs --cflags libturbojpeg`
SRC   = src/connection.c src/settings.c src/decoder*.c src/av.c src/usb.c src/queue.c
USBMUXD = `pkg-config --libs --cflags libusbmuxd-2.0`

ifneq ($(findstring ayatana,$(APPINDICATOR)),)
	CFLAGS += -DUSE_AYATANA_APPINDICATOR
endif


all: droidcam-cli droidcam

ifneq "$(RELEASE)" ""
SRC  += src/libusbmuxd.a src/libxml2.a src/libplist-2.0.a
package: clean all
	zip "droidcam_$(RELEASE).zip" \
		LICENSE README* icon2.png  \
		droidcam* install* uninstall* \
		v4l2loopback/*

else
LIBS += $(USBMUXD)
endif

gresource: .gresource.xml icon2.png
	glib-compile-resources .gresource.xml --generate-source --target=src/resources.c

droidcam-cli: LDLIBS += $(JPEG) $(LIBAV) $(LIBS)
droidcam-cli: src/droidcam-cli.c $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@ $(LDFLAGS) $(LDLIBS)

droidcam: LDLIBS += $(GTK) $(JPEG) $(LIBAV) $(LIBS)
droidcam: src/droidcam.c src/resources.c $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@ $(LDFLAGS) $(LDLIBS)

clean:
	rm -f droidcam
	rm -f droidcam-cli
	make -C v4l2loopback clean
