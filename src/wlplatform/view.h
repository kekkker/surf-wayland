#pragma once

#include <wpe/wpe-platform.h>
#include <wayland-client.h>

#define SURF_TYPE_VIEW (surf_view_get_type())
G_DECLARE_DERIVABLE_TYPE(SurfView, surf_view, SURF, VIEW, WPEView)

struct _SurfViewClass {
    WPEViewClass parent_class;
};

/* Create a dedicated wl_surface + wl_subsurface for this view. Each tab
 * keeps its own; tab switch is just a z-order swap, so the user never
 * sees a stale frame from another tab while the new one is still
 * producing its first frame post-resume. Must be called once after the
 * SurfView is constructed. */
void surf_view_realize(SurfView *view,
                       struct wl_compositor *compositor,
                       struct wl_subcompositor *subcompositor,
                       struct wl_surface *parent);

struct wl_surface *surf_view_get_wl_surface(SurfView *view);
struct wl_subsurface *surf_view_get_wl_subsurface(SurfView *view);

/* Reorder this view's subsurface above @ref. */
void surf_view_place_above(SurfView *view, struct wl_surface *ref);

/* Set the subsurface position (relative to parent). */
void surf_view_set_position(SurfView *view, int x, int y);
