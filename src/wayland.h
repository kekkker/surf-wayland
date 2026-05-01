#pragma once

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wpe/wayland/wpe-wayland.h>

typedef struct {
    WPEDisplayWayland       *wpe_display;
    struct wl_display       *display;
    struct wl_compositor    *compositor;
    struct wl_subcompositor *subcompositor;
    struct wl_shm           *shm;
    struct wl_seat          *seat;
    struct wl_pointer       *pointer;

    /* cursors */
    struct wl_cursor_theme  *cursor_theme;
    struct wl_cursor        *cursor_default;
    struct wl_cursor        *cursor_text;
    struct wl_cursor        *cursor_pointer;
    struct wl_surface       *cursor_surface;
} WaylandState;

void wayland_init(WaylandState *wl, WPEDisplayWayland *display);
void wayland_finish(WaylandState *wl);
void wayland_set_cursor(WaylandState *wl, struct wl_pointer *ptr,
    uint32_t serial, const char *name);
