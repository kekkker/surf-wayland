#pragma once

#include <wayland-client.h>
#include <wpe/wayland/wpe-wayland.h>

typedef struct {
    WPEDisplayWayland       *wpe_display;
    struct wl_display       *display;
    struct wl_compositor    *compositor;
    struct wl_subcompositor *subcompositor;
    struct wl_shm           *shm;
} WaylandState;

void wayland_init(WaylandState *wl, WPEDisplayWayland *display);
void wayland_finish(WaylandState *wl);
