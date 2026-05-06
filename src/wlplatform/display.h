#pragma once

#include <wpe/wpe-platform.h>
#include <wayland-client.h>
#include <glib.h>

/* Forward declarations for Wayland globals we own */
struct wl_compositor;
struct wl_subcompositor;
struct wl_shm;
struct xdg_wm_base;
struct zwp_linux_dmabuf_v1;

#define SURF_TYPE_DISPLAY (surf_display_get_type())
G_DECLARE_DERIVABLE_TYPE(SurfDisplay, surf_display, SURF, DISPLAY, WPEDisplay)

struct _SurfDisplayClass {
    WPEDisplayClass parent_class;
};

SurfDisplay *surf_display_new(struct wl_display      *wl_display,
                              struct wl_compositor   *compositor,
                              struct wl_subcompositor *subcompositor,
                              struct wl_shm          *shm,
                              struct xdg_wm_base     *wm_base,
                              struct zwp_linux_dmabuf_v1 *dmabuf);

/* Accessors for subclasses */
struct wl_display       *surf_display_get_wl_display(SurfDisplay *d);
struct wl_compositor    *surf_display_get_wl_compositor(SurfDisplay *d);
struct wl_subcompositor *surf_display_get_wl_subcompositor(SurfDisplay *d);
struct wl_shm           *surf_display_get_wl_shm(SurfDisplay *d);
struct xdg_wm_base      *surf_display_get_xdg_wm_base(SurfDisplay *d);
struct zwp_linux_dmabuf_v1 *surf_display_get_dmabuf(SurfDisplay *d);

void surf_display_update_screen_size(SurfDisplay *d, int width, int height);

/* Seed screen geometry/refresh from a wl_output mode event. Call after
 * surf_display_new() but before wpe_display_connect() so the values are
 * applied when the WPEScreen is created. Pass refresh_mhz=0 to keep the
 * default; scale<=0 is treated as 1. */
void surf_display_set_screen_info(SurfDisplay *d,
    int width, int height, int refresh_mhz, int scale);
