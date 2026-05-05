#include "wayland.h"

#include <stdio.h>
#include <string.h>

/* ── Seat listener ────────────────────────────────────────────────────── */

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
    WaylandState *wl = data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !wl->pointer)
        wl->pointer = wl_seat_get_pointer(seat);
    else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && wl->pointer) {
        wl_pointer_destroy(wl->pointer);
        wl->pointer = NULL;
    }
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !wl->keyboard)
        wl->keyboard = wl_seat_get_keyboard(seat);
    else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && wl->keyboard) {
        wl_keyboard_destroy(wl->keyboard);
        wl->keyboard = NULL;
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{ (void)data; (void)seat; (void)name; }

static const struct wl_seat_listener seat_listener = {
    seat_capabilities, seat_name
};

/* ── xdg_wm_base ping ─────────────────────────────────────────────────── */

static void wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = wm_base_ping,
};

/* ── Registry listener ────────────────────────────────────────────────── */

static void registry_global(void *data, struct wl_registry *reg,
    uint32_t name, const char *iface, uint32_t version)
{
    WaylandState *wl = data;
    (void)version;

    if (!strcmp(iface, wl_compositor_interface.name))
        wl->compositor = wl_registry_bind(reg, name,
            &wl_compositor_interface, 4);
    else if (!strcmp(iface, wl_subcompositor_interface.name))
        wl->subcompositor = wl_registry_bind(reg, name,
            &wl_subcompositor_interface, 1);
    else if (!strcmp(iface, wl_seat_interface.name))
        wl->seat = wl_registry_bind(reg, name, &wl_seat_interface, 7);
    else if (!strcmp(iface, wl_shm_interface.name))
        wl->shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, xdg_wm_base_interface.name))
        wl->wm_base = wl_registry_bind(reg, name, &xdg_wm_base_interface, 2);
    else if (!strcmp(iface, zwp_linux_dmabuf_v1_interface.name))
        wl->dmabuf = wl_registry_bind(reg, name,
            &zwp_linux_dmabuf_v1_interface, 4);
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

/* ── Public API ───────────────────────────────────────────────────────── */

void wayland_connect(WaylandState *wl)
{
    memset(wl, 0, sizeof *wl);

    wl->display = wl_display_connect(NULL);
    if (!wl->display)
        g_error("surf: cannot connect to Wayland display");

    wl->registry = wl_display_get_registry(wl->display);
    wl_registry_add_listener(wl->registry, &registry_listener, wl);
    wl_display_roundtrip(wl->display);

    if (wl->seat) {
        wl_seat_add_listener(wl->seat, &seat_listener, wl);
        wl_display_roundtrip(wl->display);
    }

    if (wl->wm_base)
        xdg_wm_base_add_listener(wl->wm_base, &wm_base_listener, NULL);

    if (!wl->compositor)
        g_error("surf: wl_compositor not available");
    if (!wl->subcompositor)
        fprintf(stderr, "surf: warning: wl_subcompositor unavailable\n");
    if (!wl->wm_base)
        g_error("surf: xdg_wm_base not available");

    /* Load cursor theme */
    if (wl->shm) {
        wl->cursor_theme = wl_cursor_theme_load(NULL, 24, wl->shm);
        if (wl->cursor_theme) {
            wl->cursor_default = wl_cursor_theme_get_cursor(wl->cursor_theme, "default");
            wl->cursor_text    = wl_cursor_theme_get_cursor(wl->cursor_theme, "text");
            wl->cursor_pointer = wl_cursor_theme_get_cursor(wl->cursor_theme, "hand2");
            wl->cursor_surface = wl_compositor_create_surface(wl->compositor);
        }
    }
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
    if (wl->keyboard)       wl_keyboard_destroy(wl->keyboard);
    if (wl->pointer)        wl_pointer_destroy(wl->pointer);
    if (wl->seat)           wl_seat_destroy(wl->seat);
    if (wl->dmabuf)         zwp_linux_dmabuf_v1_destroy(wl->dmabuf);
    if (wl->wm_base)        xdg_wm_base_destroy(wl->wm_base);
    if (wl->subcompositor)  wl_subcompositor_destroy(wl->subcompositor);
    if (wl->shm)            wl_shm_destroy(wl->shm);
    if (wl->compositor)     wl_compositor_destroy(wl->compositor);
    if (wl->registry)       wl_registry_destroy(wl->registry);
    if (wl->display)        wl_display_disconnect(wl->display);
}
