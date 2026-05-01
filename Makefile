# surf - simple browser
# See LICENSE file for copyright and license details.
.POSIX:

include config.mk

SRC  = src/main.c src/wayland.c src/input.c src/chrome.c src/tabs.c src/actions.c src/cmdbar.c src/download.c src/filepicker.c src/history.c
OBJ  = $(SRC:.c=.o)

WSRC = webext-surf.c
WOBJ = $(WSRC:.c=.o)
WLIB = $(WSRC:.c=.so)

all: surf $(WLIB)

surf: $(OBJ)
	$(CC) -o $@ $(OBJ) $(SURFLIBS)

src/main.o:    src/main.c    src/app.h src/input.h src/actions.h src/chrome.h src/tabs.h src/hints.h src/wayland.h src/cmdbar.h src/download.h src/history.h config.h config.mk
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ src/main.c

src/download.o: src/download.c src/download.h config.mk
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ src/download.c

src/wayland.o: src/wayland.c src/wayland.h config.mk
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ src/wayland.c

src/chrome.o:  src/chrome.c  src/chrome.h src/cmdbar.h src/history.h src/wayland.h config.mk
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ src/chrome.c

src/tabs.o:    src/tabs.c    src/tabs.h src/app.h src/hints.h src/filepicker.h src/history.h config.mk
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ src/tabs.c

src/filepicker.o: src/filepicker.c src/filepicker.h config.h config.mk
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ src/filepicker.c

src/actions.o: src/actions.c src/actions.h src/app.h src/tabs.h src/hints.h src/cmdbar.h src/download.h src/history.h config.h config.mk
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ src/actions.c

src/input.o:   src/input.c   src/input.h src/app.h src/actions.h src/hints.h src/cmdbar.h src/download.h src/history.h config.h config.mk
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ src/input.c

src/cmdbar.o:  src/cmdbar.c  src/cmdbar.h config.mk
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ src/cmdbar.c

src/history.o: src/history.c src/history.h config.mk
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ src/history.c

$(WLIB): $(WOBJ)
	$(CC) -shared -Wl,-soname,$@ -o $@ $(WOBJ) $(WEXTLIBS)

$(WOBJ): $(WSRC)
	$(CC) $(WEXTCFLAGS) -c $(WSRC)

clean:
	rm -f surf $(OBJ) $(WLIB) $(WOBJ)

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
