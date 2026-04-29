#include "wayland.h"
#include "input.h"
#include "chrome.h"
#include "tabs.h"

#include <wpe/webkit.h>
#include <wpe/wayland/wpe-wayland.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>

/* ── global app state ────────────────────────────────────────────────────── */

static WaylandState     wl;
static InputState       in;
static GMainLoop       *loop;
static TabArray         tabs;
static ChromePanel     *tabbar_panel;
static ChromePanel     *statusbar_panel;
static WPEDisplayWayland *wpe_display;
static WPEToplevel      *main_toplevel;

/* ── chrome repaint ──────────────────────────────────────────────────────── */

static void repaint_chrome(void)
{
    if (!tabbar_panel || !statusbar_panel || tabs.count == 0)
        return;

    /* Build tab list for chrome painter */
    ChromeTab *ctabs = g_new0(ChromeTab, tabs.count);
    for (int i = 0; i < tabs.count; i++) {
        Tab *t = &tabs.items[i];
        ctabs[i].title  = t->title ? t->title :
                          (t->uri  ? t->uri  : "New Tab");
        ctabs[i].active = (i == tabs.active);
        ctabs[i].pinned = t->pinned;
    }
    chrome_paint_tabbar(tabbar_panel, ctabs, tabs.count);
    chrome_panel_commit(tabbar_panel);
    g_free(ctabs);

    Tab *at = tabarray_active(&tabs);
    chrome_paint_statusbar(statusbar_panel,
        at ? (at->uri ? at->uri : "") : "",
        at ? at->progress   : 0,
        at ? at->https      : 0,
        at ? at->insecure   : 0);
    chrome_panel_commit(statusbar_panel);
}

static void on_tab_changed(void *data)
{
    (void)data;
    repaint_chrome();
}

/* ── chrome layout ───────────────────────────────────────────────────────── */

static void layout_chrome(int win_w, int win_h)
{
    if (!main_toplevel || !WPE_IS_TOPLEVEL_WAYLAND(main_toplevel))
        return;

    struct wl_surface *parent =
        wpe_toplevel_wayland_get_wl_surface(WPE_TOPLEVEL_WAYLAND(main_toplevel));

    if (!tabbar_panel) {
        tabbar_panel = chrome_panel_create(&wl, parent,
            0, 0, win_w, CHROME_TABBAR_H);
    } else {
        chrome_panel_resize(tabbar_panel, &wl, win_w, CHROME_TABBAR_H);
        chrome_panel_set_position(tabbar_panel, 0, 0);
    }

    int sbar_y = win_h - CHROME_STATUSBAR_H;
    if (!statusbar_panel) {
        statusbar_panel = chrome_panel_create(&wl, parent,
            0, sbar_y, win_w, CHROME_STATUSBAR_H);
    } else {
        chrome_panel_resize(statusbar_panel, &wl, win_w, CHROME_STATUSBAR_H);
        chrome_panel_set_position(statusbar_panel, 0, sbar_y);
    }

    repaint_chrome();
}

static void on_view_resized(WPEView *view, gpointer data)
{
    (void)data;
    layout_chrome(wpe_view_get_width(view), wpe_view_get_height(view));
}

/* ── input ───────────────────────────────────────────────────────────────── */

static gboolean handle_key(guint keyval, WPEModifiers mods, gpointer data)
{
    (void)data;
    gboolean ctrl  = (mods & WPE_MODIFIER_KEYBOARD_CONTROL) != 0;
    gboolean shift = (mods & WPE_MODIFIER_KEYBOARD_SHIFT)   != 0;

    if (ctrl) {
        switch (keyval) {
        case WPE_KEY_q:
            g_main_loop_quit(loop);
            return TRUE;

        case WPE_KEY_r:
            if (tabarray_active(&tabs))
                webkit_web_view_reload(tabarray_active(&tabs)->wv);
            return TRUE;

        case WPE_KEY_t:
            /* New tab (empty) */
            tabarray_new(&tabs, WPE_DISPLAY(wpe_display), main_toplevel,
                on_tab_changed, NULL);
            repaint_chrome();
            return TRUE;

        case WPE_KEY_w:
            /* Close active tab; quit if last */
            if (tabs.count == 1) {
                g_main_loop_quit(loop);
            } else {
                tabarray_close(&tabs, tabs.active, on_tab_changed, NULL);
                repaint_chrome();
            }
            return TRUE;

        case WPE_KEY_Tab:
            /* Cycle tabs */
            if (tabs.count > 1) {
                int next = shift
                    ? (tabs.active - 1 + tabs.count) % tabs.count
                    : (tabs.active + 1) % tabs.count;
                tabarray_switch(&tabs, next);
                repaint_chrome();
            }
            return TRUE;
        }
    }
    return FALSE;
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    const char *url = argc > 1 ? argv[1] : "about:blank";

    GError *err = NULL;
    wpe_display = WPE_DISPLAY_WAYLAND(wpe_display_wayland_new());
    if (!wpe_display_wayland_connect(wpe_display, NULL, &err))
        g_error("surf: %s", err->message);

    wayland_init(&wl, wpe_display);
    tabarray_init(&tabs);

    /* First tab — WPE auto-creates toplevel */
    Tab *first = tabarray_new(&tabs, WPE_DISPLAY(wpe_display), NULL,
        on_tab_changed, NULL);

    main_toplevel = wpe_view_get_toplevel(first->view);
    if (main_toplevel)
        wpe_toplevel_resize(main_toplevel, 1280, 800);

    g_signal_connect(first->view, "resized",
        G_CALLBACK(on_view_resized), NULL);

    input_init(&in, first->view, handle_key, NULL);

    webkit_web_view_load_uri(first->wv, url);

    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    if (tabbar_panel)   chrome_panel_destroy(tabbar_panel);
    if (statusbar_panel) chrome_panel_destroy(statusbar_panel);
    tabarray_free(&tabs);
    g_object_unref(wpe_display);
    wayland_finish(&wl);
    g_main_loop_unref(loop);
    return 0;
}
