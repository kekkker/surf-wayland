VERSION = 3.0

PREFIX    = /usr/local
MANPREFIX = $(PREFIX)/share/man
LIBPREFIX = $(PREFIX)/lib
LIBDIR    = $(LIBPREFIX)/surf

WLPROTOCOLS_DIR := $(shell pkg-config --variable=pkgdatadir wayland-protocols)
WAYLAND_SCANNER := $(shell pkg-config --variable=wayland_scanner wayland-scanner)

PKG_UI   = wayland-client wayland-cursor xkbcommon cairo pangocairo egl libdrm
PKG_WPE  = wpe-webkit-2.0 wpe-platform-2.0 glib-2.0 gio-2.0

# wpe-platform installs to /usr/local/lib64 on this system
PKG_CONFIG_PATH := /usr/local/lib64/pkgconfig:$(PKG_CONFIG_PATH)
export PKG_CONFIG_PATH
PKG_WEXT = wpe-web-process-extension-2.0 gio-2.0

CPPFLAGS += -DVERSION=\"$(VERSION)\" \
            -DLIBPREFIX=\"$(LIBPREFIX)\" \
            -DWEBEXTDIR=\"$(LIBDIR)\" \
            -D_DEFAULT_SOURCE

SURFCFLAGS  = $(shell pkg-config --cflags $(PKG_UI) $(PKG_WPE)) $(CPPFLAGS) \
              -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable \
              -Wno-sign-compare -Wno-missing-field-initializers -g -O0
SURFLIBS    = $(shell pkg-config --libs $(PKG_UI) $(PKG_WPE))

WEXTCFLAGS  = -fPIC $(shell pkg-config --cflags $(PKG_WEXT)) $(CPPFLAGS) \
              -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable \
              -Wno-sign-compare -Wno-missing-field-initializers -g -O0
WEXTLIBS    = $(shell pkg-config --libs $(PKG_WEXT))

CC = cc
