# surf version
VERSION = 2.1

# Customize below to fit your system

# paths
PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man
LIBPREFIX = $(PREFIX)/lib
LIBDIR = $(LIBPREFIX)/surf

X11INC = `pkg-config --cflags x11`
X11LIB = `pkg-config --libs x11`

# Wayland dependencies
WAYLANDINC = `pkg-config --cflags wayland-client wayland-cursor`
WAYLANDLIB = `pkg-config --libs wayland-client wayland-cursor`

# D-Bus dependencies (for Wayland IPC)
DBUSINC = `pkg-config --cflags dbus-1`
DBUSLIB = `pkg-config --libs dbus-1`

GTKINC = `pkg-config --cflags gtk+-3.0 gcr-3 webkit2gtk-4.1`
GTKLIB = `pkg-config --libs gtk+-3.0 gcr-3 webkit2gtk-4.1`
WEBEXTINC = `pkg-config --cflags webkit2gtk-4.1 webkit2gtk-web-extension-4.1 gio-2.0`
WEBEXTLIBS = `pkg-config --libs webkit2gtk-4.1 webkit2gtk-web-extension-4.1 gio-2.0`

# Conditional compilation
# X11 = 1        # Uncomment for X11 support
# WAYLAND = 1    # Uncomment for Wayland support

# includes and libs
ifeq ($(X11),1)
    INCS = $(X11INC) $(GTKINC)
    LIBS = $(X11LIB) $(GTKLIB) -lgthread-2.0
    CPPFLAGS += -DX11_SUPPORT
endif

ifeq ($(WAYLAND),1)
    INCS = $(WAYLANDINC) $(DBUSINC) $(GTKINC)
    LIBS = $(WAYLANDLIB) $(DBUSLIB) $(GTKLIB) -lgthread-2.0
    CPPFLAGS += -DWAYLAND_SUPPORT
endif

# Default to both X11 and Wayland support if neither is specified
ifeq ($(X11)$(WAYLAND),)
    INCS = $(X11INC) $(WAYLANDINC) $(DBUSINC) $(GTKINC)
    LIBS = $(X11LIB) $(WAYLANDLIB) $(DBUSLIB) $(GTKLIB) -lgthread-2.0
    CPPFLAGS += -DX11_SUPPORT -DWAYLAND_SUPPORT
endif

# flags
CPPFLAGS += -DVERSION=\"$(VERSION)\" -DGCR_API_SUBJECT_TO_CHANGE \
            -DLIBPREFIX=\"$(LIBPREFIX)\" -DWEBEXTDIR=\"$(LIBDIR)\" \
            -D_DEFAULT_SOURCE
SURFCFLAGS = -fPIC $(INCS) $(CPPFLAGS)
WEBEXTCFLAGS = -fPIC $(WEBEXTINC)

# compiler
#CC = c99
