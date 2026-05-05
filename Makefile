# surf - simple browser
# See LICENSE file for copyright and license details.
.POSIX:

include config.mk

SRC  = src/main.c src/wayland.c src/input.c src/chrome.c src/tabs.c \
       src/actions.c src/cmdbar.c src/download.c src/filepicker.c src/history.c
OBJ  = $(SRC:.c=.o)

# Custom WPE platform (wlplatform)
WPSRC = src/wlplatform/display.c src/wlplatform/view.c src/wlplatform/toplevel.c
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

src/protocols/xdg-shell.o: src/protocols/xdg-shell.c config.mk
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ $<

src/protocols/linux-dmabuf-v1.o: src/protocols/linux-dmabuf-v1.c config.mk
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ $<

# ── main sources ─────────────────────────────────────────────────────────

src/main.o: src/main.c src/app.h src/input.h src/actions.h src/chrome.h \
            src/tabs.h src/hints.h src/wayland.h src/cmdbar.h src/download.h \
            src/history.h src/wlplatform/display.h src/wlplatform/view.h \
            src/wlplatform/toplevel.h config.h config.mk
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ src/main.c

src/download.o: src/download.c src/download.h config.mk
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ src/download.c

src/wayland.o: src/wayland.c src/wayland.h config.mk \
               src/protocols/xdg-shell-client-protocol.h \
               src/protocols/linux-dmabuf-v1-client-protocol.h
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ src/wayland.c

src/chrome.o: src/chrome.c src/chrome.h src/cmdbar.h src/history.h src/wayland.h config.mk
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ src/chrome.c

src/tabs.o: src/tabs.c src/tabs.h src/app.h src/hints.h src/filepicker.h src/history.h config.mk
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ src/tabs.c

src/filepicker.o: src/filepicker.c src/filepicker.h config.h config.mk
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ src/filepicker.c

src/actions.o: src/actions.c src/actions.h src/app.h src/tabs.h src/hints.h \
               src/cmdbar.h src/download.h src/history.h config.h config.mk
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ src/actions.c

src/input.o: src/input.c src/input.h src/app.h src/actions.h src/hints.h \
             src/cmdbar.h src/download.h src/history.h config.h config.mk
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ src/input.c

src/cmdbar.o: src/cmdbar.c src/cmdbar.h config.mk
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ src/cmdbar.c

src/history.o: src/history.c src/history.h config.mk
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ src/history.c

# ── wlplatform (custom WPE platform) ─────────────────────────────────────

src/wlplatform/display.o: src/wlplatform/display.c src/wlplatform/display.h \
                           src/wlplatform/view.h src/wlplatform/toplevel.h \
                           src/protocols/xdg-shell-client-protocol.h \
                           src/protocols/linux-dmabuf-v1-client-protocol.h config.mk
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ src/wlplatform/display.c

src/wlplatform/view.o: src/wlplatform/view.c src/wlplatform/view.h \
                        src/wlplatform/display.h \
                        src/protocols/linux-dmabuf-v1-client-protocol.h config.mk
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ src/wlplatform/view.c

src/wlplatform/toplevel.o: src/wlplatform/toplevel.c src/wlplatform/toplevel.h \
                            src/wlplatform/display.h config.mk
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ src/wlplatform/toplevel.c

# ── web extension ────────────────────────────────────────────────────────

$(WLIB): $(WOBJ)
	$(CC) -shared -Wl,-soname,$@ -o $@ $(WOBJ) $(WEXTLIBS)

$(WOBJ): $(WSRC)
	$(CC) $(WEXTCFLAGS) -c $(WSRC)

clean:
	rm -f surf $(OBJ) $(WPOBJ) $(WLIB) $(WOBJ) $(PROTO_SRC)

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
