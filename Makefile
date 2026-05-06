# surf - simple browser
# See LICENSE file for copyright and license details.
.POSIX:

include config.mk

SRC  = src/main.c src/wayland.c src/input.c src/chrome.c src/tabs.c \
       src/actions.c src/cmdbar.c src/download.c src/filepicker.c src/history.c \
       src/clipboard.c
OBJ  = $(SRC:.c=.o)

# Custom WPE platform (wlplatform)
WPSRC = src/wlplatform/display.c src/wlplatform/view.c src/wlplatform/toplevel.c \
        src/wlplatform/screen.c src/wlplatform/clipboard.c
WPOBJ = $(WPSRC:.c=.o)

# Protocol headers (generated from wayland-protocols XML)
PROTO_SRC = src/protocols/xdg-shell.c src/protocols/linux-dmabuf-v1.c
PROTO_OBJ = $(PROTO_SRC:.c=.o)

WSRC = webext-surf.c
WOBJ = $(WSRC:.c=.o)
WLIB = $(WSRC:.c=.so)

all: surf $(WLIB)

surf: $(OBJ) $(WPOBJ) $(PROTO_OBJ)
	$(CC) -o $@ $(OBJ) $(WPOBJ) $(PROTO_OBJ) $(SURFLIBS)

# ── protocol generation ──────────────────────────────────────────────────

src/protocols/xdg-shell-client-protocol.h: $(WLPROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml
	$(WAYLAND_SCANNER) client-header $< $@

src/protocols/xdg-shell.c: $(WLPROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml
	$(WAYLAND_SCANNER) public-code $< $@

src/protocols/linux-dmabuf-v1-client-protocol.h: $(WLPROTOCOLS_DIR)/stable/linux-dmabuf/linux-dmabuf-v1.xml
	$(WAYLAND_SCANNER) client-header $< $@

src/protocols/linux-dmabuf-v1.c: $(WLPROTOCOLS_DIR)/stable/linux-dmabuf/linux-dmabuf-v1.xml
	$(WAYLAND_SCANNER) public-code $< $@

# ── pattern rule with auto-generated header deps ────────────────────────
#
# `-MMD -MP` emits a `.d` file alongside each `.o` listing every header
# the source pulled in. Including those `.d` files below makes header
# changes propagate without us hand-listing deps per target — without
# this, adding a field to e.g. wayland.h silently leaves stale .o files
# whose struct layouts no longer match the rest of the binary.

DEPS = $(OBJ:.o=.d) $(WPOBJ:.o=.d) $(PROTO_OBJ:.o=.d)

%.o: %.c config.mk
	$(CC) $(SURFCFLAGS) -Isrc -MMD -MP -c -o $@ $<

src/protocols/xdg-shell.o: src/protocols/xdg-shell.c
src/protocols/linux-dmabuf-v1.o: src/protocols/linux-dmabuf-v1.c

src/main.o: src/protocols/xdg-shell-client-protocol.h \
            src/protocols/linux-dmabuf-v1-client-protocol.h
src/wayland.o: src/protocols/xdg-shell-client-protocol.h \
               src/protocols/linux-dmabuf-v1-client-protocol.h
src/wlplatform/display.o: src/protocols/xdg-shell-client-protocol.h \
                          src/protocols/linux-dmabuf-v1-client-protocol.h
src/wlplatform/view.o: src/protocols/linux-dmabuf-v1-client-protocol.h

-include $(DEPS)

# ── web extension ────────────────────────────────────────────────────────

$(WLIB): $(WOBJ)
	$(CC) -shared -Wl,-soname,$@ -o $@ $(WOBJ) $(WEXTLIBS)

$(WOBJ): $(WSRC)
	$(CC) $(WEXTCFLAGS) -c $(WSRC)

clean:
	rm -f surf $(OBJ) $(WPOBJ) $(WLIB) $(WOBJ) $(PROTO_SRC) $(DEPS)

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f surf $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/surf
	mkdir -p $(DESTDIR)$(LIBDIR)
	cp -f $(WLIB) $(DESTDIR)$(LIBDIR)
	chmod 644 $(DESTDIR)$(LIBDIR)/$(WLIB)
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	sed "s/VERSION/$(VERSION)/g" < surf.1 > $(DESTDIR)$(MANPREFIX)/man1/surf.1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/surf.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/surf
	rm -f $(DESTDIR)$(MANPREFIX)/man1/surf.1
	rm -f $(DESTDIR)$(LIBDIR)/$(WLIB)
	- rmdir $(DESTDIR)$(LIBDIR)

.PHONY: all clean install uninstall
