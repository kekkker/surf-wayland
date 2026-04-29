#include "wayland.h"
#include "input.h"

#include <wpe/webkit.h>
#include <wpe/wayland/wpe-wayland.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>

static WaylandState  wl;
static InputState    in;
static GMainLoop    *loop;

static gboolean handle_key(guint keyval, WPEModifiers mods, gpointer data)
{
    (void)data;
    if (keyval == WPE_KEY_q &&
        (mods & WPE_MODIFIER_KEYBOARD_CONTROL))
    {
        g_main_loop_quit(loop);
        return TRUE;
    }
    return FALSE;
}

static void on_load_changed(WebKitWebView *wv, WebKitLoadEvent ev, gpointer data)
{
    (void)data;
    if (ev != WEBKIT_LOAD_COMMITTED)
        return;

    WPEView    *view = webkit_web_view_get_wpe_view(wv);
    WPEToplevel *tl  = view ? wpe_view_get_toplevel(view) : NULL;

    if (!tl || !WPE_IS_TOPLEVEL_WAYLAND(tl)) {
        fprintf(stderr, "surf: no WPEToplevelWayland after load\n");
        return;
    }

    printf("surf: toplevel wl_surface=%p subcompositor=%p\n",
        (void *)wpe_toplevel_wayland_get_wl_surface(WPE_TOPLEVEL_WAYLAND(tl)),
        (void *)wl.subcompositor);
    fflush(stdout);
}

int main(int argc, char *argv[])
{
    const char *url = argc > 1 ? argv[1] : "about:blank";

    GError *err = NULL;
    WPEDisplayWayland *display = WPE_DISPLAY_WAYLAND(wpe_display_wayland_new());
    if (!wpe_display_wayland_connect(display, NULL, &err))
        g_error("surf: Wayland connect: %s", err->message);

    wayland_init(&wl, display);

    WebKitWebView *wv = g_object_new(WEBKIT_TYPE_WEB_VIEW,
        "display", WPE_DISPLAY(display),
        NULL);

    WPEView *view = webkit_web_view_get_wpe_view(wv);
    wpe_view_map(view);

    WPEToplevel *tl = wpe_view_get_toplevel(view);
    if (tl)
        wpe_toplevel_resize(tl, 1280, 800);

    input_init(&in, view, handle_key, NULL);
    g_signal_connect(wv, "load-changed", G_CALLBACK(on_load_changed), NULL);

    printf("surf: loading %s\n", url);
    fflush(stdout);
    webkit_web_view_load_uri(wv, url);

    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    g_object_unref(wv);
    g_object_unref(display);
    wayland_finish(&wl);
    g_main_loop_unref(loop);
    return 0;
}
