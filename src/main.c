#include "wayland.h"
#include "input.h"
#include "chrome.h"

#include <wpe/webkit.h>
#include <wpe/wayland/wpe-wayland.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>

static WaylandState  wl;
static InputState    in;
static GMainLoop    *loop;

static ChromePanel  *tabbar;
static ChromePanel  *statusbar;
static WPEView      *main_view;
static WebKitWebView *main_wv;

static const char   *page_title    = NULL;
static const char   *page_uri      = NULL;
static int           page_progress = 0;
static int           page_https    = 0;
static int           page_insecure = 0;

/* ── Chrome repaint ─────────────────────────────────────────────────────── */

static void repaint_chrome(void)
{
    if (!tabbar || !statusbar)
        return;

    ChromeTab tab = {
        .title  = page_title ? page_title : (page_uri ? page_uri : "New Tab"),
        .active = 1,
        .pinned = 0,
    };
    chrome_paint_tabbar(tabbar, &tab, 1);
    chrome_panel_commit(tabbar);

    chrome_paint_statusbar(statusbar,
        page_uri ? page_uri : "",
        page_progress, page_https, page_insecure);
    chrome_panel_commit(statusbar);
}

/* ── Chrome setup / resize ──────────────────────────────────────────────── */

static void setup_chrome(int win_w, int win_h)
{
    WPEToplevel *tl = wpe_view_get_toplevel(main_view);
    if (!tl || !WPE_IS_TOPLEVEL_WAYLAND(tl))
        return;

    struct wl_surface *parent =
        wpe_toplevel_wayland_get_wl_surface(WPE_TOPLEVEL_WAYLAND(tl));

    if (!tabbar) {
        tabbar = chrome_panel_create(&wl, parent,
            0, 0, win_w, CHROME_TABBAR_H);
    } else {
        chrome_panel_resize(tabbar, &wl, win_w, CHROME_TABBAR_H);
        chrome_panel_set_position(tabbar, 0, 0);
    }

    int sbar_y = win_h - CHROME_STATUSBAR_H;
    if (!statusbar) {
        statusbar = chrome_panel_create(&wl, parent,
            0, sbar_y, win_w, CHROME_STATUSBAR_H);
    } else {
        chrome_panel_resize(statusbar, &wl, win_w, CHROME_STATUSBAR_H);
        chrome_panel_set_position(statusbar, 0, sbar_y);
    }

    repaint_chrome();
}

static void on_view_resized(WPEView *view, gpointer data)
{
    (void)data;
    int w = wpe_view_get_width(view);
    int h = wpe_view_get_height(view);
    setup_chrome(w, h);
}

/* ── WebKit signals ─────────────────────────────────────────────────────── */

static void on_load_changed(WebKitWebView *wv, WebKitLoadEvent ev, gpointer data)
{
    (void)data;
    if (ev == WEBKIT_LOAD_STARTED) {
        page_progress = 0;
        page_https    = 0;
        page_insecure = 0;
    }
    page_uri = webkit_web_view_get_uri(wv);
    repaint_chrome();
}

static void on_notify_progress(GObject *obj, GParamSpec *pspec, gpointer data)
{
    (void)pspec; (void)data;
    page_progress = (int)(webkit_web_view_get_estimated_load_progress(
        WEBKIT_WEB_VIEW(obj)) * 100);
    repaint_chrome();
}

static void on_notify_title(GObject *obj, GParamSpec *pspec, gpointer data)
{
    (void)pspec; (void)data;
    page_title = webkit_web_view_get_title(WEBKIT_WEB_VIEW(obj));
    repaint_chrome();
}

/* ── Input ──────────────────────────────────────────────────────────────── */

static gboolean handle_key(guint keyval, WPEModifiers mods, gpointer data)
{
    (void)data;
    if (keyval == WPE_KEY_q && (mods & WPE_MODIFIER_KEYBOARD_CONTROL)) {
        g_main_loop_quit(loop);
        return TRUE;
    }
    /* Reload: Ctrl+R */
    if (keyval == WPE_KEY_r && (mods & WPE_MODIFIER_KEYBOARD_CONTROL)) {
        webkit_web_view_reload(main_wv);
        return TRUE;
    }
    return FALSE;
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    const char *url = argc > 1 ? argv[1] : "about:blank";

    GError *err = NULL;
    WPEDisplayWayland *display = WPE_DISPLAY_WAYLAND(wpe_display_wayland_new());
    if (!wpe_display_wayland_connect(display, NULL, &err))
        g_error("surf: %s", err->message);

    wayland_init(&wl, display);

    main_wv = g_object_new(WEBKIT_TYPE_WEB_VIEW,
        "display", WPE_DISPLAY(display), NULL);

    main_view = webkit_web_view_get_wpe_view(main_wv);
    wpe_view_map(main_view);

    WPEToplevel *tl = wpe_view_get_toplevel(main_view);
    if (tl)
        wpe_toplevel_resize(tl, 1280, 800);

    g_signal_connect(main_view, "resized",
        G_CALLBACK(on_view_resized), NULL);
    g_signal_connect(main_wv, "load-changed",
        G_CALLBACK(on_load_changed), NULL);
    g_signal_connect(main_wv, "notify::estimated-load-progress",
        G_CALLBACK(on_notify_progress), NULL);
    g_signal_connect(main_wv, "notify::title",
        G_CALLBACK(on_notify_title), NULL);

    input_init(&in, main_view, handle_key, NULL);

    webkit_web_view_load_uri(main_wv, url);

    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    if (tabbar)   chrome_panel_destroy(tabbar);
    if (statusbar) chrome_panel_destroy(statusbar);
    g_object_unref(main_wv);
    g_object_unref(display);
    wayland_finish(&wl);
    g_main_loop_unref(loop);
    return 0;
}
