#include "app.h"
#include "input.h"
#include "actions.h"
#include "../config.h"
#include "tabs.h"
#include "chrome.h"
#include "wayland.h"

#include <wpe/webkit.h>
#include <wpe/wayland/wpe-wayland.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>

/* ── global app state ────────────────────────────────────────────────────── */

AppState g_app;

static InputState in;

/* ── chrome ──────────────────────────────────────────────────────────────── */

void app_repaint_chrome(void)
{
    if (!g_app.tabbar || !g_app.statusbar || g_app.tabs.count == 0)
        return;

    ChromeTab *ctabs = g_new0(ChromeTab, g_app.tabs.count);
    for (int i = 0; i < g_app.tabs.count; i++) {
        Tab *t = &g_app.tabs.items[i];
        ctabs[i].title  = t->title ? t->title
                        : (t->uri  ? t->uri : "New Tab");
        ctabs[i].active = (i == g_app.tabs.active);
        ctabs[i].pinned = t->pinned;
    }
    chrome_paint_tabbar(g_app.tabbar, ctabs, g_app.tabs.count);
    chrome_panel_commit(g_app.tabbar);
    g_free(ctabs);

    Tab *at = app_active_tab();
    if (g_app.cmdbar.mode != CMDBAR_INACTIVE) {
        chrome_paint_cmdbar(g_app.statusbar, &g_app.cmdbar);
    } else {
        chrome_paint_statusbar(g_app.statusbar,
            at ? (at->uri ? at->uri : "") : "",
            at ? at->progress  : 0,
            at ? at->https     : 0,
            at ? at->insecure  : 0);
    }
    chrome_panel_commit(g_app.statusbar);
}

void app_layout_chrome(int win_w, int win_h)
{
    if (!g_app.toplevel || !WPE_IS_TOPLEVEL_WAYLAND(g_app.toplevel))
        return;

    struct wl_surface *parent =
        wpe_toplevel_wayland_get_wl_surface(WPE_TOPLEVEL_WAYLAND(g_app.toplevel));

    if (!g_app.tabbar) {
        g_app.tabbar = chrome_panel_create(&g_app.wl, parent,
            0, 0, win_w, CHROME_TABBAR_H);
    } else {
        chrome_panel_resize(g_app.tabbar, &g_app.wl, win_w, CHROME_TABBAR_H);
        chrome_panel_set_position(g_app.tabbar, 0, 0);
    }

    int sbar_y = win_h - CHROME_STATUSBAR_H;
    if (!g_app.statusbar) {
        g_app.statusbar = chrome_panel_create(&g_app.wl, parent,
            0, sbar_y, win_w, CHROME_STATUSBAR_H);
    } else {
        chrome_panel_resize(g_app.statusbar, &g_app.wl, win_w, CHROME_STATUSBAR_H);
        chrome_panel_set_position(g_app.statusbar, 0, sbar_y);
    }

    app_repaint_chrome();
}

static void on_view_resized(WPEView *view, gpointer data)
{
    (void)data;
    app_layout_chrome(wpe_view_get_width(view), wpe_view_get_height(view));
}

static void on_view_closed(WPEView *view, gpointer data)
{
    (void)view; (void)data;
    g_main_loop_quit(g_app.loop);
}

static void tab_changed_cb(void *d)
{
    (void)d;
    app_repaint_chrome();
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    const char *url = argc > 1 ? argv[1] : "about:blank";

    GError *err = NULL;
    g_app.display = WPE_DISPLAY_WAYLAND(wpe_display_wayland_new());
    if (!wpe_display_wayland_connect(g_app.display, NULL, &err))
        g_error("surf: %s", err->message);

    wayland_init(&g_app.wl, g_app.display);
    tabarray_init(&g_app.tabs);

    /* First tab — WPE auto-creates toplevel */
    Tab *first = tabarray_new(&g_app.tabs, WPE_DISPLAY(g_app.display), NULL,
        tab_changed_cb, NULL);

    g_app.toplevel = wpe_view_get_toplevel(first->view);
    if (g_app.toplevel)
        wpe_toplevel_resize(g_app.toplevel, winsize[0], winsize[1]);

    g_signal_connect(first->view, "resized",
        G_CALLBACK(on_view_resized), NULL);
    g_signal_connect(first->view, "closed",
        G_CALLBACK(on_view_closed), NULL);

    input_init(&in, first->view, NULL, NULL);

    webkit_web_view_load_uri(first->wv, url);

    g_app.loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(g_app.loop);

    /* Destroy our Wayland objects before WPE closes the connection */
    if (g_app.tabbar)    chrome_panel_destroy(g_app.tabbar);
    if (g_app.statusbar) chrome_panel_destroy(g_app.statusbar);
    wayland_finish(&g_app.wl);

    tabarray_free(&g_app.tabs);
    g_object_unref(g_app.display);
    g_main_loop_unref(g_app.loop);
    return 0;
}
