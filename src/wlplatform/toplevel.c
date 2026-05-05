#include "toplevel.h"
#include "../app.h"

#include <wpe/wpe-platform.h>
#include <stdio.h>

/* ── Private data ─────────────────────────────────────────────────────── */

typedef struct _SurfToplevelPrivate {
    int saved_w;
    int saved_h;
} SurfToplevelPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SurfToplevel, surf_toplevel, WPE_TYPE_TOPLEVEL)

/* ── WPEToplevel vfuncs ───────────────────────────────────────────────── */

static void surf_toplevel_set_title(WPEToplevel *toplevel, const char *title)
{
    /* Forward to our xdg_toplevel */
    (void)toplevel;
    if (g_app.xdg_toplevel && title)
        xdg_toplevel_set_title(g_app.xdg_toplevel, title);
}

static WPEScreen *surf_toplevel_get_screen(WPEToplevel *toplevel)
{
    /* TODO: track wl_output */
    (void)toplevel;
    return NULL;
}

static gboolean surf_toplevel_resize(WPEToplevel *toplevel, int w, int h)
{
    /* On a tiling compositor, client resize is advisory and usually ignored.
     * We could send xdg_toplevel.set_min/max_size as hints, but for now
     * just reject — WPE handles this gracefully. */
    (void)toplevel; (void)w; (void)h;
    return FALSE;
}

static gboolean surf_toplevel_set_fullscreen(WPEToplevel *toplevel, gboolean fullscreen)
{
    (void)toplevel;
    if (!g_app.xdg_toplevel)
        return FALSE;

    if (fullscreen)
        xdg_toplevel_set_fullscreen(g_app.xdg_toplevel, NULL);
    else
        xdg_toplevel_unset_fullscreen(g_app.xdg_toplevel);
    return TRUE;
}

static gboolean surf_toplevel_set_maximized(WPEToplevel *toplevel, gboolean maximized)
{
    (void)toplevel;
    if (!g_app.xdg_toplevel)
        return FALSE;

    if (maximized)
        xdg_toplevel_set_maximized(g_app.xdg_toplevel);
    else
        xdg_toplevel_unset_maximized(g_app.xdg_toplevel);
    return TRUE;
}

static gboolean surf_toplevel_set_minimized(WPEToplevel *toplevel)
{
    (void)toplevel;
    if (!g_app.xdg_toplevel)
        return FALSE;
    xdg_toplevel_set_minimized(g_app.xdg_toplevel);
    return TRUE;
}

static WPEBufferFormats *surf_toplevel_get_preferred_buffer_formats(WPEToplevel *toplevel)
{
    /* Delegate to display */
    WPEDisplay *display = wpe_toplevel_get_display(toplevel);
    return wpe_display_get_preferred_buffer_formats(display);
}

/* ── GObject ──────────────────────────────────────────────────────────── */

static void surf_toplevel_class_init(SurfToplevelClass *klass)
{
    WPEToplevelClass *tclass = WPE_TOPLEVEL_CLASS(klass);
    tclass->set_title = surf_toplevel_set_title;
    tclass->get_screen = surf_toplevel_get_screen;
    tclass->resize = surf_toplevel_resize;
    tclass->set_fullscreen = surf_toplevel_set_fullscreen;
    tclass->set_maximized = surf_toplevel_set_maximized;
    tclass->set_minimized = surf_toplevel_set_minimized;
    tclass->get_preferred_buffer_formats = surf_toplevel_get_preferred_buffer_formats;
}

static void surf_toplevel_init(SurfToplevel *self)
{
}

/* ── Public API ───────────────────────────────────────────────────────── */

SurfToplevel *surf_toplevel_new(WPEDisplay *display, guint max_views)
{
    return SURF_TOPLEVEL(g_object_new(SURF_TYPE_TOPLEVEL,
        "display", display, "max-views", max_views, NULL));
}
