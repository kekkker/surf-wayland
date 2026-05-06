#pragma once

#include <wpe/wpe-platform.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define SURF_TYPE_SCREEN (surf_screen_get_type())
G_DECLARE_FINAL_TYPE(SurfScreen, surf_screen, SURF, SCREEN, WPEScreen)

/* Construct a SurfScreen for the given numeric id. The screen's refresh
 * rate, size, and scale should be set via the standard wpe_screen_set_*
 * accessors after creation. The sync observer reads refresh_rate at
 * start() time, so set it before WebKit attaches a callback. */
WPEScreen *surf_screen_new(guint id);

G_END_DECLS
