# surf version
VERSION = 2.1

# paths
PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man
LIBPREFIX = $(PREFIX)/lib
LIBDIR = $(LIBPREFIX)/surf

# Wayland + GTK + WebKit
GTKINC = `pkg-config --cflags gtk+-3.0 gcr-3 webkit2gtk-4.1 gdk-wayland-3.0`
GTKLIB = `pkg-config --libs gtk+-3.0 gcr-3 webkit2gtk-4.1 gdk-wayland-3.0`
WEBEXTINC = `pkg-config --cflags webkit2gtk-4.1 webkit2gtk-web-extension-4.1 gio-2.0`
WEBEXTLIBS = `pkg-config --libs webkit2gtk-4.1 webkit2gtk-web-extension-4.1 gio-2.0`

# flags
CPPFLAGS += -DVERSION=\"$(VERSION)\" \
            -DGCR_API_SUBJECT_TO_CHANGE \
            -DGCK_API_SUBJECT_TO_CHANGE \
            -DWAYLAND_SUPPORT \
            -DLIBPREFIX=\"$(LIBPREFIX)\" \
            -DWEBEXTDIR=\"$(LIBDIR)\" \
            -D_DEFAULT_SOURCE

SURFCFLAGS = -fPIC $(GTKINC) $(CPPFLAGS) \
             -Wall -Wextra -Wno-unused-parameter \
             -Wno-sign-compare -Wno-missing-field-initializers -g -O1

WEBEXTCFLAGS = -fPIC $(WEBEXTINC) $(CPPFLAGS) \
               -Wall -Wextra -Wno-unused-parameter \
               -Wno-sign-compare -Wno-missing-field-initializers -g -O1

LIBS = $(GTKLIB)
SURFLDFLAGS =

CC = cc
