#pragma once

#include <wpe/wpe-platform.h>
#include <wayland-client.h>

#define SURF_TYPE_VIEW (surf_view_get_type())
G_DECLARE_DERIVABLE_TYPE(SurfView, surf_view, SURF, VIEW, WPEView)

struct _SurfViewClass {
    WPEViewClass parent_class;
};

/* Each SurfView owns its own wl_surface + wl_subsurface (mirrors GTK's
 * per-widget surface model). Tab switch is a z-order swap — each tab's
 * wl_surface keeps its own last frame, so switching is instant and
 * doesn't depend on WebKit producing a fresh frame. */
void surf_view_realize(SurfView *view,
                       struct wl_compositor *compositor,
                       struct wl_subcompositor *subcompositor,
                       struct wl_shm *shm,
                       struct wl_surface *parent,
                       int initial_w, int initial_h);

struct wl_surface *surf_view_get_wl_surface(SurfView *view);
struct wl_subsurface *surf_view_get_wl_subsurface(SurfView *view);

void surf_view_place_above(SurfView *view, struct wl_surface *ref);
void surf_view_set_position(SurfView *view, int x, int y);
