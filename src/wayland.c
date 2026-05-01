#include "wayland.h"

#include <stdio.h>
#include <string.h>

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
    WaylandState *wl = data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !wl->pointer)
        wl->pointer = wl_seat_get_pointer(seat);
    else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && wl->pointer) {
        wl_pointer_destroy(wl->pointer);
        wl->pointer = NULL;
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{ (void)data; (void)seat; (void)name; }

static const struct wl_seat_listener seat_listener = {
    seat_capabilities, seat_name
};

static void registry_global(void *data, struct wl_registry *reg,
    uint32_t name, const char *iface, uint32_t version)
{
    WaylandState *wl = data;
    (void)version;
    if (!strcmp(iface, wl_subcompositor_interface.name))
        wl->subcompositor = wl_registry_bind(reg, name,
            &wl_subcompositor_interface, 1);
    else if (!strcmp(iface, wl_seat_interface.name))
        wl->seat = wl_registry_bind(reg, name, &wl_seat_interface, 1);
}

static void registry_global_remove(void *data, struct wl_registry *reg,
    uint32_t name)
{
    (void)data; (void)reg; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global        = registry_global,
    .global_remove = registry_global_remove,
};

void wayland_init(WaylandState *wl, WPEDisplayWayland *display)
{
    wl->wpe_display  = display;
    wl->display      = wpe_display_wayland_get_wl_display(display);
    wl->compositor   = wpe_display_wayland_get_wl_compositor(display);
    wl->shm          = wpe_display_wayland_get_wl_shm(display);

    struct wl_registry *reg = wl_display_get_registry(wl->display);
    wl_registry_add_listener(reg, &registry_listener, wl);
    wl_display_roundtrip(wl->display);
    wl_registry_destroy(reg);

    if (wl->seat) {
        wl_seat_add_listener(wl->seat, &seat_listener, wl);
        wl_display_roundtrip(wl->display);  /* triggers seat_capabilities → wl->pointer */
    }

    /* Load cursor theme */
    wl->cursor_theme = wl_cursor_theme_load(NULL, 24, wl->shm);
    if (wl->cursor_theme) {
        wl->cursor_default = wl_cursor_theme_get_cursor(wl->cursor_theme, "default");
        wl->cursor_text    = wl_cursor_theme_get_cursor(wl->cursor_theme, "text");
        wl->cursor_pointer = wl_cursor_theme_get_cursor(wl->cursor_theme, "hand2");
        wl->cursor_surface = wl_compositor_create_surface(wl->compositor);
    }

    if (!wl->subcompositor)
        fprintf(stderr, "surf: wl_subcompositor unavailable\n");
}

void wayland_set_cursor(WaylandState *wl, struct wl_pointer *ptr,
    uint32_t serial, const char *name)
{
    if (!wl->cursor_theme || !wl->cursor_surface) return;
    struct wl_cursor *c = NULL;
    if (strcmp(name, "text") == 0)        c = wl->cursor_text;
    else if (strcmp(name, "pointer") == 0) c = wl->cursor_pointer;
    else                                   c = wl->cursor_default;
    if (!c) c = wl->cursor_default;
    if (!c) return;
    struct wl_cursor_image *img = c->images[0];
    wl_surface_attach(wl->cursor_surface,
        wl_cursor_image_get_buffer(img), 0, 0);
    wl_surface_commit(wl->cursor_surface);
    wl_pointer_set_cursor(ptr, serial, wl->cursor_surface,
        (int32_t)img->hotspot_x, (int32_t)img->hotspot_y);
}

void wayland_finish(WaylandState *wl)
{
    if (wl->cursor_surface) wl_surface_destroy(wl->cursor_surface);
    if (wl->cursor_theme)   wl_cursor_theme_destroy(wl->cursor_theme);
    if (wl->pointer)        wl_pointer_destroy(wl->pointer);
    if (wl->seat)           wl_seat_destroy(wl->seat);
    if (wl->subcompositor)  wl_subcompositor_destroy(wl->subcompositor);
}
