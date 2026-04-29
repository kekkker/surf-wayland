#include "actions.h"
#include "app.h"
#include "tabs.h"
#include "cmdbar.h"

#include <wpe/webkit.h>
#include <wpe/wpe-platform.h>
#include <stdio.h>

static void tab_changed_cb(void *d) { (void)d; app_repaint_chrome(); }

/* ── navigation ──────────────────────────────────────────────────────────── */

void act_navigate(const Arg *a)
{
    Tab *t = app_active_tab();
    if (!t) return;
    if (a->i < 0) webkit_web_view_go_back(t->wv);
    else           webkit_web_view_go_forward(t->wv);
}

void act_reload(const Arg *a)
{
    Tab *t = app_active_tab();
    if (!t) return;
    if (a->i)
        webkit_web_view_reload_bypass_cache(t->wv);
    else
        webkit_web_view_reload(t->wv);
}

void act_stop(const Arg *a)
{
    (void)a;
    Tab *t = app_active_tab();
    if (t) webkit_web_view_stop_loading(t->wv);
}

/* ── scroll / zoom ───────────────────────────────────────────────────────── */

void act_scrollv(const Arg *a)
{
    Tab *t = app_active_tab();
    if (!t) return;
    char js[128];
    snprintf(js, sizeof js,
        "document.scrollingElement.scrollTop+=window.innerHeight/100*%d;",
        a->i);
    webkit_web_view_evaluate_javascript(t->wv, js, -1,
        NULL, NULL, NULL, NULL, NULL);
}

void act_scrollh(const Arg *a)
{
    Tab *t = app_active_tab();
    if (!t) return;
    char js[128];
    snprintf(js, sizeof js,
        "window.scrollBy(window.innerWidth/100*%d,0);", a->i);
    webkit_web_view_evaluate_javascript(t->wv, js, -1,
        NULL, NULL, NULL, NULL, NULL);
}

void act_zoom(const Arg *a)
{
    Tab *t = app_active_tab();
    if (!t) return;
    double cur = webkit_web_view_get_zoom_level(t->wv);
    if      (a->i > 0) webkit_web_view_set_zoom_level(t->wv, cur + 0.1);
    else if (a->i < 0) webkit_web_view_set_zoom_level(t->wv, cur - 0.1);
    else               webkit_web_view_set_zoom_level(t->wv, 1.0);
}

/* ── find ────────────────────────────────────────────────────────────────── */

void act_find_next(const Arg *a)
{
    Tab *t = app_active_tab();
    if (!t || !t->finder) return;
    if (a->i > 0)
        webkit_find_controller_search_next(t->finder);
    else
        webkit_find_controller_search_previous(t->finder);
    app_repaint_chrome();
}

/* ── window / tabs ───────────────────────────────────────────────────────── */

void act_fullscreen(const Arg *a)
{
    (void)a;
    if (!g_app.toplevel) return;
    if (g_app.fullscreen) {
        wpe_toplevel_unfullscreen(g_app.toplevel);
        g_app.fullscreen = 0;
    } else {
        wpe_toplevel_fullscreen(g_app.toplevel);
        g_app.fullscreen = 1;
    }
}

void act_new_tab(const Arg *a)
{
    (void)a;
    tabarray_new(&g_app.tabs, WPE_DISPLAY(g_app.display), g_app.toplevel,
        tab_changed_cb, g_app.tab_close_fn, NULL);
    app_repaint_chrome();
}

void act_close_tab(const Arg *a)
{
    (void)a;
    if (g_app.tabs.count == 1) {
        g_main_loop_quit(g_app.loop);
        return;
    }
    tabarray_close(&g_app.tabs, g_app.tabs.active, tab_changed_cb, NULL);
    app_repaint_chrome();
}

void act_switch_tab(const Arg *a)
{
    int n = g_app.tabs.count;
    if (n <= 1) return;
    int next = (g_app.tabs.active + a->i + n) % n;
    tabarray_switch(&g_app.tabs, next);
    app_repaint_chrome();
}

void act_quit(const Arg *a)
{
    (void)a;
    g_main_loop_quit(g_app.loop);
}

/* ── mode ────────────────────────────────────────────────────────────────── */

void act_insert_mode(const Arg *a)
{
    (void)a;
    Tab *t = app_active_tab();
    if (t) {
        t->mode = MODE_INSERT;
        app_repaint_chrome();
    }
}

void act_normal_mode(const Arg *a)
{
    (void)a;
    Tab *t = app_active_tab();
    if (!t) return;
    if (t->mode == MODE_INSERT || t->mode == MODE_SEARCH ||
        t->mode == MODE_COMMAND) {
        if (t->mode == MODE_SEARCH && t->finder)
            webkit_find_controller_search_finish(t->finder);
        if (t->mode == MODE_COMMAND || t->mode == MODE_SEARCH)
            cmdbar_close(&g_app.cmdbar);
        t->mode = MODE_NORMAL;
        wpe_view_focus_in(t->view);
        app_repaint_chrome();
    }
    webkit_web_view_stop_loading(t->wv);
}

void act_open_bar(const Arg *a)
{
    Tab *t = app_active_tab();
    if (!t) return;
    CmdBarMode mode = (a->i == 2) ? CMDBAR_URL_NEWTAB : CMDBAR_URL;
    const char *prefill = (a->i == 1) ? t->uri : NULL;
    cmdbar_open(&g_app.cmdbar, mode, prefill);
    t->mode = MODE_COMMAND;
    app_repaint_chrome();
}

void act_open_search(const Arg *a)
{
    (void)a;
    Tab *t = app_active_tab();
    if (!t) return;
    cmdbar_open(&g_app.cmdbar, CMDBAR_SEARCH, NULL);
    t->mode = MODE_SEARCH;
    app_repaint_chrome();
}
