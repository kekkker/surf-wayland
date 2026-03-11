# surf version
VERSION = 2.1

# paths
PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man
LIBPREFIX = $(PREFIX)/lib
LIBDIR = $(LIBPREFIX)/surf

# Wayland + GTK4 + WebKit 6
GTKINC = `pkg-config --cflags gtk4 webkitgtk-6.0`
GTKLIB = `pkg-config --libs gtk4 webkitgtk-6.0`
WEBEXTINC = `pkg-config --cflags webkitgtk-web-process-extension-6.0 gio-2.0`
WEBEXTLIBS = `pkg-config --libs webkitgtk-web-process-extension-6.0 gio-2.0`

# flags
CPPFLAGS += -DVERSION=\"$(VERSION)\" \
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
SURFLDFLAGS = -rdynamic

CC = cc
