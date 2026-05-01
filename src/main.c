#include "app.h"
#include "input.h"
#include "actions.h"
#include "../config.h"
#include "tabs.h"
#include "chrome.h"
#include "wayland.h"
#include "download.h"

#include <wpe/webkit.h>
#include <wpe/wayland/wpe-wayland.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <linux/input-event-codes.h>

/* ── global app state ────────────────────────────────────────────────────── */

AppState g_app;

/* download.c reads this directly when committing destination */
char *g_dl_pending_path;

static InputState in;

static char *expand_home_path(const char *path)
{
    if (!path || !*path)
        return NULL;
    if (path[0] == '~')
        return g_strconcat(g_get_home_dir(), path + 1, NULL);
    return g_strdup(path);
}

static int cmdbar_panel_height(void)
{
    if (g_app.cmdbar.mode == CMDBAR_INACTIVE)
        return CHROME_STATUSBAR_H;
    return CHROME_CMDROW_H;
}

static void app_get_layout_size(int *w, int *h)
{
    Tab *t = app_active_tab();
    int ww = g_app.window_w > 0 ? g_app.window_w : winsize[0];
    int wh = g_app.window_h > 0 ? g_app.window_h : winsize[1];

    if (t && t->view) {
        int vw = wpe_view_get_width(t->view);
        int vh = wpe_view_get_height(t->view);
        if (vw > 100)
            ww = vw;
        if (vh > (CHROME_TABBAR_H + CHROME_STATUSBAR_H + 100))
            wh = vh;
    }
    if (g_app.tabbar && g_app.tabbar->width > 0)
        ww = g_app.tabbar->width;

    *w = ww;
    *h = wh;
}

void app_relayout_active(void)
{
    int w, h;

    app_get_layout_size(&w, &h);
    app_layout_chrome(w, h);
}

static void cmdbar_set_text(const char *text)
{
    int n;

    if (!text)
        text = "";
    n = (int)strlen(text);
    if (n >= CMDBAR_MAXLEN)
        n = CMDBAR_MAXLEN - 1;
    memcpy(g_app.cmdbar.buf, text, n);
    g_app.cmdbar.buf[n] = '\0';
    g_app.cmdbar.len = n;
    g_app.cmdbar.cursor = n;
}

void app_cmdbar_clear_history(void)
{
    int old_count = g_app.history_match_count;

    g_app.history_match_count = 0;
    g_app.history_match_selected = -1;

    if (old_count > 0)
        app_relayout_active();
}

void app_cmdbar_refresh_history(void)
{
    Tab *t = app_active_tab();
    int old_count = g_app.history_match_count;
    const char *text = cmdbar_text(&g_app.cmdbar);

    if (!t || t->mode != MODE_COMMAND ||
        (g_app.cmdbar.mode != CMDBAR_URL &&
         g_app.cmdbar.mode != CMDBAR_URL_NEWTAB)) {
        app_cmdbar_clear_history();
    } else {
        g_app.history_match_count = history_collect_matches(&g_app.history,
            text, g_app.history_matches, HISTORY_MAX_MATCHES);
        g_app.history_match_selected = -1;
    }

    if (old_count != g_app.history_match_count)
        app_relayout_active();
}

gboolean app_cmdbar_select_history(int direction)
{
    if (g_app.history_match_count <= 0)
        return FALSE;

    if (g_app.history_match_selected < 0) {
        g_app.history_match_selected = direction > 0
            ? 0 : g_app.history_match_count - 1;
    } else {
        g_app.history_match_selected += direction;
        if (g_app.history_match_selected < 0)
            g_app.history_match_selected = g_app.history_match_count - 1;
        if (g_app.history_match_selected >= g_app.history_match_count)
            g_app.history_match_selected = 0;
    }

    cmdbar_set_text(g_app.history_matches[g_app.history_match_selected].uri);
    return TRUE;
}

/* ── chrome ──────────────────────────────────────────────────────────────── */

static void repaint_dlbar(void)
{
    if (!g_app.dlbar || g_app.dls.count == 0) return;
    int n = g_app.dls.count;
    if (n > CHROME_DLBAR_MAX_ROWS) n = CHROME_DLBAR_MAX_ROWS;
    char **lines = g_new(char *, n);
    /* show newest at top */
    for (int i = 0; i < n; i++)
        lines[i] = download_format_line(
            &g_app.dls.items[g_app.dls.count - 1 - i]);
    chrome_paint_dlbar(g_app.dlbar, lines, n);
    chrome_panel_commit(g_app.dlbar);
    for (int i = 0; i < n; i++) g_free(lines[i]);
    g_free(lines);
}

void app_repaint_chrome(void)
{
    if (!g_app.tabbar || !g_app.statusbar || g_app.tabs.count == 0)
        return;
    repaint_dlbar();

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
        chrome_paint_cmdbar(g_app.statusbar, &g_app.cmdbar,
            g_app.history_matches, g_app.history_match_count,
            g_app.history_match_selected);
        if (g_app.historybar && g_app.history_match_count > 0) {
            int visible_rows = g_app.historybar->height / CHROME_CMDROW_H;
            if (visible_rows > g_app.history_match_count)
                visible_rows = g_app.history_match_count;
            chrome_paint_history(g_app.historybar, g_app.history_matches,
                visible_rows, g_app.history_match_selected);
            chrome_panel_commit(g_app.historybar);
        }
    } else {
        const char *uri = at
            ? (at->hover_uri ? at->hover_uri : (at->uri ? at->uri : ""))
            : "";
        const char *modestr = "";
        if (at) {
            switch (at->mode) {
            case MODE_INSERT:  modestr = "INSERT"; break;
            case MODE_COMMAND: modestr = "COMMAND"; break;
            case MODE_SEARCH:  modestr = "SEARCH"; break;
            case MODE_HINT:    modestr = "HINT"; break;
            case MODE_SELECT:  modestr = "SELECT"; break;
            default: break;
            }
        }
        chrome_paint_statusbar(g_app.statusbar, uri,
            at ? at->progress  : 0,
            at ? at->https     : 0,
            at ? at->insecure  : 0,
            modestr,
            at ? at->find_current_match : 0,
            at ? at->find_match_count   : 0);
    }
    chrome_panel_commit(g_app.statusbar);
}

void app_layout_chrome(int win_w, int win_h)
{
    if (!g_app.toplevel || !WPE_IS_TOPLEVEL_WAYLAND(g_app.toplevel))
        return;

    /* Fall back to last known good size for spurious zero/tiny events
     * (e.g. "resized" emitted with 0 dims when a view is unmapped). */
    if (win_w <= 100)
        win_w = g_app.window_w > 0 ? g_app.window_w : winsize[0];
    if (win_h <= CHROME_TABBAR_H + CHROME_STATUSBAR_H + 100)
        win_h = g_app.window_h > 0 ? g_app.window_h : winsize[1];

    g_app.window_w = win_w;
    g_app.window_h = win_h;

    struct wl_surface *parent =
        wpe_toplevel_wayland_get_wl_surface(WPE_TOPLEVEL_WAYLAND(g_app.toplevel));

    if (!g_app.tabbar) {
        g_app.tabbar = chrome_panel_create(&g_app.wl, parent,
            0, 0, win_w, CHROME_TABBAR_H);
    } else {
        chrome_panel_resize(g_app.tabbar, &g_app.wl, win_w, CHROME_TABBAR_H);
        chrome_panel_set_position(g_app.tabbar, 0, 0);
    }

    /* dlbar — created on demand when downloads exist; sized to row count */
    if (g_app.dls.count > 0) {
        int rows = g_app.dls.count;
        if (rows > CHROME_DLBAR_MAX_ROWS) rows = CHROME_DLBAR_MAX_ROWS;
        int dlh = rows * CHROME_DLROW_H;
        if (!g_app.dlbar) {
            g_app.dlbar = chrome_panel_create(&g_app.wl, parent,
                0, CHROME_TABBAR_H, win_w, dlh);
        } else {
            chrome_panel_resize(g_app.dlbar, &g_app.wl, win_w, dlh);
            chrome_panel_set_position(g_app.dlbar, 0, CHROME_TABBAR_H);
        }
    } else if (g_app.dlbar) {
        chrome_panel_destroy(g_app.dlbar);
        g_app.dlbar = NULL;
    }

    int sbar_h = cmdbar_panel_height();
    int sbar_y = win_h - sbar_h;
    if (!g_app.statusbar) {
        g_app.statusbar = chrome_panel_create(&g_app.wl, parent,
            0, sbar_y, win_w, sbar_h);
    } else {
        chrome_panel_resize(g_app.statusbar, &g_app.wl, win_w, sbar_h);
        chrome_panel_set_position(g_app.statusbar, 0, sbar_y);
    }

    if (g_app.cmdbar.mode != CMDBAR_INACTIVE && g_app.history_match_count > 0) {
        int max_rows = sbar_y / CHROME_CMDROW_H;
        int visible_rows = g_app.history_match_count;
        if (max_rows < 1)
            max_rows = 1;
        if (visible_rows > max_rows)
            visible_rows = max_rows;
        int hh = visible_rows * CHROME_CMDROW_H;
        int hy = sbar_y - hh;
        if (hy < 0)
            hy = 0;
        if (!g_app.historybar) {
            g_app.historybar = chrome_panel_create(&g_app.wl, parent,
                0, hy, win_w, hh);
        } else {
            chrome_panel_resize(g_app.historybar, &g_app.wl, win_w, hh);
            chrome_panel_set_position(g_app.historybar, 0, hy);
        }
        wl_subsurface_place_above(g_app.historybar->subsurface,
            g_app.statusbar->surface);
    } else if (g_app.historybar) {
        chrome_panel_destroy(g_app.historybar);
        g_app.historybar = NULL;
    }

    app_repaint_chrome();
    /* Subsurface positions are pending parent state; commit parent so they
     * take effect even when WebKit has nothing new to render (about:blank). */
    wl_surface_commit(parent);
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

/* ── downloads ───────────────────────────────────────────────────────────── */

static void dl_changed_cb(void *d)
{
    (void)d;
    /* Layout may need to (re)create the dlbar panel based on count */
    if (g_app.toplevel && WPE_IS_TOPLEVEL_WAYLAND(g_app.toplevel)) {
        WPEView *v = NULL;
        Tab *t = app_active_tab();
        if (t) v = t->view;
        if (v)
            app_layout_chrome(wpe_view_get_width(v), wpe_view_get_height(v));
    }
    app_repaint_chrome();
}

static void dl_need_path_cb(const char *uri, const char *suggested, void *d)
{
    (void)d;
    Tab *t = app_active_tab();
    if (!t) return;
    g_free(g_app.dl_pending_uri);
    g_app.dl_pending_uri = g_strdup(uri);

    const char *home = g_get_home_dir();
    const char *dldir = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    if (!dldir) dldir = home;
    char *defpath = g_build_filename(dldir, suggested, NULL);
    app_cmdbar_clear_history();
    cmdbar_open(&g_app.cmdbar, CMDBAR_DOWNLOAD, defpath);
    g_free(defpath);
    t->mode = MODE_COMMAND;
    app_repaint_chrome();
}

static void tab_close_cb(int idx, void *data)
{
    (void)data;
    if (g_app.tabs.count == 1) {
        g_main_loop_quit(g_app.loop);
        return;
    }
    tabarray_close(&g_app.tabs, idx, tab_changed_cb, NULL);
    app_repaint_chrome();
}

/* ── pointer: tab clicks ─────────────────────────────────────────────────── */

static struct wl_surface *ptr_surface;
static int ptr_x;

static void ptr_enter(void *d, struct wl_pointer *p, uint32_t ser,
    struct wl_surface *surf, wl_fixed_t x, wl_fixed_t y)
{
    (void)d; (void)y;
    ptr_surface = surf;
    ptr_x = wl_fixed_to_int(x);
    /* Set cursor: default on chrome surfaces, text on page */
    if (g_app.tabbar && surf == g_app.tabbar->surface)
        wayland_set_cursor(&g_app.wl, p, ser, "default");
    else if (g_app.statusbar && surf == g_app.statusbar->surface)
        wayland_set_cursor(&g_app.wl, p, ser, "default");
    else if (g_app.dlbar && surf == g_app.dlbar->surface)
        wayland_set_cursor(&g_app.wl, p, ser, "default");
    else
        wayland_set_cursor(&g_app.wl, p, ser, "text");
}

static void ptr_leave(void *d, struct wl_pointer *p, uint32_t ser,
    struct wl_surface *surf)
{
    (void)d; (void)p; (void)ser; (void)surf;
    ptr_surface = NULL;
}

static void ptr_motion(void *d, struct wl_pointer *p, uint32_t t,
    wl_fixed_t x, wl_fixed_t y)
{
    (void)d; (void)p; (void)t; (void)y;
    ptr_x = wl_fixed_to_int(x);
}

static void ptr_button(void *d, struct wl_pointer *p, uint32_t ser,
    uint32_t t, uint32_t btn, uint32_t state)
{
    (void)d; (void)p; (void)ser; (void)t;
    if (state != WL_POINTER_BUTTON_STATE_PRESSED) return;
    if (!g_app.tabbar || ptr_surface != g_app.tabbar->surface) return;

    int n = g_app.tabs.count;
    if (n == 0) return;
    int tw = g_app.tabbar->width / n;
    if (tw > 200) tw = 200;
    int idx = ptr_x / tw;
    if (idx < 0 || idx >= n) return;

    if (btn == BTN_LEFT) {
        tabarray_switch(&g_app.tabs, idx);
        app_repaint_chrome();
    } else if (btn == BTN_MIDDLE) {
        tab_close_cb(idx, NULL);
    }
}

static void ptr_axis(void *d, struct wl_pointer *p, uint32_t t,
    uint32_t axis, wl_fixed_t v)
{ (void)d; (void)p; (void)t; (void)axis; (void)v; }

static const struct wl_pointer_listener pointer_listener = {
    .enter  = ptr_enter,
    .leave  = ptr_leave,
    .motion = ptr_motion,
    .button = ptr_button,
    .axis   = ptr_axis,
};

/* ── crash handler ───────────────────────────────────────────────────────── */

static void crashhandler(int sig, siginfo_t *info, void *ctx)
{
    void *frames[64];
    int nframes, fd;
    char path[PATH_MAX];
    char buf[128];
    const char *home;
    time_t t;
    ssize_t wr __attribute__((unused));

    (void)ctx;

    home = getenv("HOME");
    if (!home)
        home = "/tmp";
    snprintf(path, sizeof(path), "%s/.surf/crash.log", home);

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        fd = STDERR_FILENO;

    t = time(NULL);
    snprintf(buf, sizeof(buf), "surf crash: signal %d at %s", sig, ctime(&t));
    wr = write(fd, buf, strlen(buf));

    if (info && (sig == SIGSEGV || sig == SIGBUS)) {
        snprintf(buf, sizeof(buf), "fault addr: %p\n", info->si_addr);
        wr = write(fd, buf, strlen(buf));
    }

    wr = write(fd, "backtrace:\n", 11);
    nframes = backtrace(frames, 64);
    backtrace_symbols_fd(frames, nframes, fd);

    if (fd != STDERR_FILENO)
        close(fd);

    signal(sig, SIG_DFL);
    raise(sig);
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

    WebKitWebContext *ctx = webkit_web_context_get_default();
    webkit_web_context_set_web_process_extensions_directory(ctx, WEBEXTDIR);

    char *cache_path = expand_home_path(cachedir);
    char *cookie_path = expand_home_path(cookiefile);
    if (cache_path) {
        g_mkdir_with_parents(cache_path, 0700);
        g_app.network_session = webkit_network_session_new(cache_path, cache_path);
    }
    if (!g_app.network_session)
        g_app.network_session = webkit_network_session_get_default();
    if (cookie_path && g_app.network_session) {
        char *dir = g_path_get_dirname(cookie_path);
        g_mkdir_with_parents(dir, 0700);
        g_free(dir);
        webkit_cookie_manager_set_persistent_storage(
            webkit_network_session_get_cookie_manager(g_app.network_session),
            cookie_path, WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);
    }
    g_free(cache_path);
    g_free(cookie_path);

    /* Pre-create the shared toplevel with unlimited views (max_views=0).
     * WPE auto-creates a per-WebView toplevel with max_views=1, which
     * silently rejects wpe_view_set_toplevel when the target is "full". */
    g_app.toplevel = wpe_display_create_toplevel(WPE_DISPLAY(g_app.display), 0);
    if (g_app.toplevel)
        wpe_toplevel_resize(g_app.toplevel, winsize[0], winsize[1]);

    tabarray_init(&g_app.tabs);
    downloads_init(&g_app.dls);
    history_state_init(&g_app.history);
    settings_init();
    g_app.tab_close_fn = tab_close_cb;

    Tab *first = tabarray_new(&g_app.tabs, WPE_DISPLAY(g_app.display), g_app.toplevel,
        tab_changed_cb, tab_close_cb, NULL);

    g_signal_connect(first->view, "resized",
        G_CALLBACK(on_view_resized), NULL);
    g_signal_connect(first->view, "closed",
        G_CALLBACK(on_view_closed), NULL);

    /* Wire downloads on the shared NetworkSession. */
    WebKitNetworkSession *ns = g_app.network_session
        ? g_app.network_session
        : webkit_web_view_get_network_session(first->wv);
    if (ns)
        downloads_attach_session(&g_app.dls, ns,
            dl_changed_cb, dl_need_path_cb, NULL);

    if (g_app.wl.pointer)
        wl_pointer_add_listener(g_app.wl.pointer, &pointer_listener, NULL);

    input_init(&in, first->view, NULL, NULL);

    webkit_web_view_load_uri(first->wv, url);

    struct sigaction sa = {0};
    sa.sa_sigaction = crashhandler;
    sa.sa_flags     = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);

    g_app.loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(g_app.loop);

    /* Destroy our Wayland objects before WPE closes the connection */
    if (g_app.tabbar)    chrome_panel_destroy(g_app.tabbar);
    if (g_app.historybar) chrome_panel_destroy(g_app.historybar);
    if (g_app.statusbar) chrome_panel_destroy(g_app.statusbar);
    if (g_app.dlbar)     chrome_panel_destroy(g_app.dlbar);
    wayland_finish(&g_app.wl);

    downloads_free(&g_app.dls);
    history_state_free(&g_app.history);
    g_free(g_app.dl_pending_uri);
    tabarray_free(&g_app.tabs);
    if (g_app.network_session &&
        g_app.network_session != webkit_network_session_get_default())
        g_object_unref(g_app.network_session);
    g_object_unref(g_app.display);
    g_main_loop_unref(g_app.loop);
    return 0;
}
