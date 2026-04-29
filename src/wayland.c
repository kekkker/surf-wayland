#include "wayland.h"

#include <stdio.h>
#include <string.h>

static void registry_global(void *data, struct wl_registry *reg,
    uint32_t name, const char *iface, uint32_t version)
{
    WaylandState *wl = data;
    (void)version;
    if (!strcmp(iface, wl_subcompositor_interface.name))
        wl->subcompositor = wl_registry_bind(reg, name,
            &wl_subcompositor_interface, 1);
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

    if (!wl->subcompositor)
        fprintf(stderr, "surf: wl_subcompositor unavailable\n");
}

void wayland_finish(WaylandState *wl)
{
    if (wl->subcompositor)
        wl_subcompositor_destroy(wl->subcompositor);
}
