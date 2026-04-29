CC = cc
PKGS = wpe-webkit-2.0 wpe-platform-wayland-2.0 glib-2.0

CFLAGS  = $(shell pkg-config --cflags $(PKGS)) -Wall -Wextra -g
LDFLAGS = $(shell pkg-config --libs   $(PKGS))

spike: spike.c
	$(CC) $(CFLAGS) -o spike spike.c $(LDFLAGS)

clean:
	rm -f spike

.PHONY: clean
