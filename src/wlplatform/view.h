#pragma once

#include <wpe/wpe-platform.h>
#include <wayland-client.h>

#define SURF_TYPE_VIEW (surf_view_get_type())
G_DECLARE_DERIVABLE_TYPE(SurfView, surf_view, SURF, VIEW, WPEView)

struct _SurfViewClass {
    WPEViewClass parent_class;
};

/* Called from app code to set up the Wayland surface/subsurface
 * for this view. The view will render WPE buffers into this subsurface. */
void surf_view_set_wl_surface(SurfView *view,
                              struct wl_surface *surface,
                              struct wl_subsurface *subsurface);

/* Disconnect this view from the shared wl_surface so another tab
 * can take ownership.  Keeps imported wl_buffers alive so the
 * compositor keeps showing the old frame (no background flash). */
void surf_view_clear_wl_surface(SurfView *view);

/* Destroy all imported wl_buffers.  Call when a tab is being closed,
 * not on switch — the WPEBuffers they wrap are about to be freed. */
void surf_view_destroy_buffers(SurfView *view);

struct wl_surface *surf_view_get_wl_surface(SurfView *view);
struct wl_subsurface *surf_view_get_wl_subsurface(SurfView *view);
