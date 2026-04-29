/*
 * Phase 0 spike: prove WPE WebKit renders via WPE Platform on Wayland.
 *
 * Verifies:
 *  1. wpe_display_wayland_new() + connect works
 *  2. WebKitWebView created via "display" GObject property (WPE Platform API)
 *  3. Page loads and renders
 *  4. We can retrieve wl_display, WPEToplevel, WPEView, wl_surface after map
 */

#include <wpe/webkit.h>
#include <wpe/wayland/wpe-wayland.h>
#include <glib.h>
#include <stdio.h>

static void on_load_changed(WebKitWebView *wv, WebKitLoadEvent ev, gpointer user_data)
{
    if (ev == WEBKIT_LOAD_STARTED)
        g_print("[spike] load started\n");
    else if (ev == WEBKIT_LOAD_COMMITTED)
        g_print("[spike] load committed\n");
    else if (ev == WEBKIT_LOAD_FINISHED) {
        g_print("[spike] load finished: %s\n", webkit_web_view_get_uri(wv));

        WPEView *wpe_view = webkit_web_view_get_wpe_view(wv);
        WPEToplevel *toplevel = wpe_view ? wpe_view_get_toplevel(wpe_view) : NULL;

        g_print("[spike] WPEView    = %p\n", (void *)wpe_view);
        g_print("[spike] WPEToplevel= %p\n", (void *)toplevel);

        if (toplevel && WPE_IS_TOPLEVEL_WAYLAND(toplevel)) {
            struct wl_surface *surf =
                wpe_toplevel_wayland_get_wl_surface(WPE_TOPLEVEL_WAYLAND(toplevel));
            g_print("[spike] toplevel wl_surface = %p\n", (void *)surf);
        }
        if (wpe_view && WPE_IS_VIEW_WAYLAND(wpe_view)) {
            struct wl_surface *surf =
                wpe_view_wayland_get_wl_surface(WPE_VIEW_WAYLAND(wpe_view));
            g_print("[spike] view wl_surface     = %p\n", (void *)surf);
        }
    }
}

int main(int argc, char *argv[])
{
    const char *url = argc > 1 ? argv[1] : "https://example.com";

    GError *err = NULL;
    WPEDisplay *display = wpe_display_wayland_new();
    if (!wpe_display_wayland_connect(WPE_DISPLAY_WAYLAND(display), NULL, &err)) {
        g_error("[spike] Wayland connect failed: %s", err->message);
    }

    struct wl_display *wl_disp =
        wpe_display_wayland_get_wl_display(WPE_DISPLAY_WAYLAND(display));
    g_print("[spike] wl_display  = %p\n", (void *)wl_disp);
    g_print("[spike] wl_compositor = %p\n",
            (void *)wpe_display_wayland_get_wl_compositor(WPE_DISPLAY_WAYLAND(display)));

    WebKitWebView *wv = g_object_new(WEBKIT_TYPE_WEB_VIEW,
        "display", display,
        NULL);

    g_signal_connect(wv, "load-changed", G_CALLBACK(on_load_changed), NULL);

    WPEView *wpe_view = webkit_web_view_get_wpe_view(wv);
    if (wpe_view) {
        wpe_view_map(wpe_view);
        WPEToplevel *tl = wpe_view_get_toplevel(wpe_view);
        if (tl)
            wpe_toplevel_resize(tl, 1280, 800);
    }

    webkit_web_view_load_uri(wv, url);

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    g_object_unref(wv);
    g_object_unref(display);
    return 0;
}
