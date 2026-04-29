# surf - simple browser
# See LICENSE file for copyright and license details.
.POSIX:

include config.mk

SRC  = src/main.c src/wayland.c src/input.c src/chrome.c
OBJ  = $(SRC:.c=.o)

# webext-surf.c ported in Phase 8; excluded until then
WSRC = webext-surf.c
WOBJ = $(WSRC:.c=.o)
WLIB = $(WSRC:.c=.so)

all: surf

surf: $(OBJ)
	$(CC) -o $@ $(OBJ) $(SURFLIBS)

src/main.o:    src/main.c    src/wayland.h src/input.h src/chrome.h config.mk
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ src/main.c

src/wayland.o: src/wayland.c src/wayland.h config.mk
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ src/wayland.c

src/input.o:   src/input.c   src/input.h config.mk
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ src/input.c

src/chrome.o:  src/chrome.c  src/chrome.h src/wayland.h config.mk
	$(CC) $(SURFCFLAGS) -Isrc -c -o $@ src/chrome.c

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
