#pragma once

#include <wpe/wpe-platform.h>

#define SURF_TYPE_TOPLEVEL (surf_toplevel_get_type())
G_DECLARE_DERIVABLE_TYPE(SurfToplevel, surf_toplevel, SURF, TOPLEVEL, WPEToplevel)

struct _SurfToplevelClass {
    WPEToplevelClass parent_class;
};

SurfToplevel *surf_toplevel_new(WPEDisplay *display, guint max_views);
