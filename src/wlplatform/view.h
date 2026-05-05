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

/* Disconnect this view from the Wayland surface. Clears all pending
 * frame callbacks and imported wl_buffers so the surface can be
 * handed to another tab without conflicts. */
void surf_view_clear_wl_surface(SurfView *view);

struct wl_surface *surf_view_get_wl_surface(SurfView *view);
struct wl_subsurface *surf_view_get_wl_subsurface(SurfView *view);
