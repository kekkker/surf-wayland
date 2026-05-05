#include "app.h"
#include "input.h"
#include "actions.h"
#include "../config.h"
#include "tabs.h"
#include "chrome.h"
#include "wayland.h"
#include "download.h"
#include "wlplatform/display.h"
#include "wlplatform/view.h"
#include "wlplatform/toplevel.h"

#include <wpe/webkit.h>
#include <wpe/wpe-platform.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/memfd.h>
#include <limits.h>
#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon.h>
#include <wpe/WPEKeymapXKB.h>

/* ── global app state ────────────────────────────────────────────────────── */

AppState g_app;
char *g_dl_pending_path;

static InputState in;

/* ── helpers ─────────────────────────────────────────────────────────────── */

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

void app_relayout_active(void)
{
    int w = g_app.window_w > 0 ? g_app.window_w : winsize[0];
    int h = g_app.window_h > 0 ? g_app.window_h : winsize[1];
    app_layout(w, h);
}

static void cmdbar_set_text(const char *text)
{
    if (!text) text = "";
    int n = (int)strlen(text);
    if (n >= CMDBAR_MAXLEN) n = CMDBAR_MAXLEN - 1;
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
    app_raise_chrome();
}

void app_raise_chrome(void)
{
    /* Re-stack all chrome panels above the WPE view subsurface.
     * Without this the web content (created after chrome) overlaps chrome. */
    struct wl_surface *ref = g_app.view_surface;
    if (!ref) return;
    ChromePanel *panels[] = { g_app.tabbar, g_app.statusbar,
                              g_app.dlbar, g_app.historybar };
    for (int i = 0; i < 4; i++) {
        if (panels[i] && panels[i]->subsurface) {
            wl_subsurface_place_above(panels[i]->subsurface, ref);
            ref = panels[i]->surface;
        }
    }
}

/* ── root surface buffer ─────────────────────────────────────────────────── */

static void attach_root_buffer(int w, int h)
{
    if (g_app.root_buffer && w == g_app.root_buf_w && h == g_app.root_buf_h)
        return;

    if (g_app.root_buffer)
        wl_buffer_destroy(g_app.root_buffer);

    int stride = w * 4;
    int sz = stride * h;
    int fd = (int)syscall(SYS_memfd_create, "surf-root", MFD_CLOEXEC);
    if (fd < 0) return;
    if (ftruncate(fd, sz) < 0) { close(fd); return; }
    void *px = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (px == MAP_FAILED) { close(fd); return; }
    memset(px, 0, sz);  /* transparent ARGB */

    struct wl_shm_pool *pool = wl_shm_create_pool(g_app.wl.shm, fd, sz);
    g_app.root_buffer = wl_shm_pool_create_buffer(pool, 0,
        w, h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    munmap(px, sz);
    close(fd);

    wl_surface_attach(g_app.root_surface, g_app.root_buffer, 0, 0);
    g_app.root_buf_w = w;
    g_app.root_buf_h = h;
}

/* ── layout ──────────────────────────────────────────────────────────────── */

void app_layout(int W, int H)
{
    if (!g_app.root_surface || !g_app.configured)
        return;

    if (W <= 100)
        W = g_app.window_w > 0 ? g_app.window_w : winsize[0];
    if (H <= CHROME_TABBAR_H + CHROME_STATUSBAR_H + 100)
        H = g_app.window_h > 0 ? g_app.window_h : winsize[1];

    g_app.window_w = W;
    g_app.window_h = H;

    attach_root_buffer(W, H);

    /* Tabbar — top */
    if (!g_app.tabbar) {
        g_app.tabbar = chrome_panel_create(&g_app.wl, g_app.root_surface,
            0, 0, W, CHROME_TABBAR_H);
    } else {
        chrome_panel_resize(g_app.tabbar, &g_app.wl, W, CHROME_TABBAR_H);
        chrome_panel_set_position(g_app.tabbar, 0, 0);
    }

    int top = CHROME_TABBAR_H;

    /* Dlbar — conditional, below tabbar */
    if (g_app.dls.count > 0) {
        int rows = g_app.dls.count;
        if (rows > CHROME_DLBAR_MAX_ROWS) rows = CHROME_DLBAR_MAX_ROWS;
        int dlh = rows * CHROME_DLROW_H;
        if (!g_app.dlbar) {
            g_app.dlbar = chrome_panel_create(&g_app.wl, g_app.root_surface,
                0, top, W, dlh);
        } else {
            chrome_panel_resize(g_app.dlbar, &g_app.wl, W, dlh);
            chrome_panel_set_position(g_app.dlbar, 0, top);
        }
        top += dlh;
    } else if (g_app.dlbar) {
        chrome_panel_destroy(g_app.dlbar);
        g_app.dlbar = NULL;
    }

    /* Statusbar — bottom */
    int sbar_h = cmdbar_panel_height();
    int sbar_y = H - sbar_h;
    if (!g_app.statusbar) {
        g_app.statusbar = chrome_panel_create(&g_app.wl, g_app.root_surface,
            0, sbar_y, W, sbar_h);
    } else {
        chrome_panel_resize(g_app.statusbar, &g_app.wl, W, sbar_h);
        chrome_panel_set_position(g_app.statusbar, 0, sbar_y);
    }

    int bottom = sbar_h;

    /* Historybar — conditional, above statusbar */
    if (g_app.cmdbar.mode != CMDBAR_INACTIVE && g_app.history_match_count > 0) {
        int max_rows = sbar_y / CHROME_CMDROW_H;
        int visible_rows = g_app.history_match_count;
        if (max_rows < 1) max_rows = 1;
        if (visible_rows > max_rows) visible_rows = max_rows;
        int hh = visible_rows * CHROME_CMDROW_H;
        int hy = sbar_y - hh;
        if (hy < 0) hy = 0;
        if (!g_app.historybar) {
            g_app.historybar = chrome_panel_create(&g_app.wl, g_app.root_surface,
                0, hy, W, hh);
        } else {
            chrome_panel_resize(g_app.historybar, &g_app.wl, W, hh);
            chrome_panel_set_position(g_app.historybar, 0, hy);
        }
        bottom += hh;
    } else if (g_app.historybar) {
        chrome_panel_destroy(g_app.historybar);
        g_app.historybar = NULL;
    }

    /* WPE view — fills the middle */
    int view_x = 0, view_y = top;
    int view_w = W, view_h = H - top - bottom;
    if (view_h < 1) view_h = 1;

    if (g_app.view_subsurface)
        wl_subsurface_set_position(g_app.view_subsurface, view_x, view_y);

    /* Tell WPE its new drawing area */
    if (g_app.toplevel)
        wpe_toplevel_resized(g_app.toplevel, view_w, view_h);

    g_app.view_x = view_x;
    g_app.view_y = view_y;
    g_app.view_w = view_w;
    g_app.view_h = view_h;

    app_repaint_chrome();
    wl_surface_commit(g_app.root_surface);
}

/* Legacy compat — just delegates to app_layout */
void app_layout_chrome(int win_w, int win_h)
{
    app_layout(win_w, win_h);
}

/* ── xdg_toplevel configure ──────────────────────────────────────────────── */

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
    int32_t w, int32_t h, struct wl_array *states)
{
    (void)data; (void)toplevel; (void)states;
    g_app.pending_w = w > 0 ? w : winsize[0];
    g_app.pending_h = h > 0 ? h : winsize[1];
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
    (void)data; (void)toplevel;
    g_main_loop_quit(g_app.loop);
}

static void xdg_surface_configure(void *data, struct xdg_surface *surface, uint32_t serial)
{
    xdg_surface_ack_configure(surface, serial);

    if (!g_app.configured) {
        g_app.configured = TRUE;
    }

    if (g_app.pending_w != g_app.window_w || g_app.pending_h != g_app.window_h ||
        !g_app.configured) {
        g_app.window_w = g_app.pending_w;
        g_app.window_h = g_app.pending_h;
        app_layout(g_app.window_w, g_app.window_h);
    }
}

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *shell, uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(shell, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close     = xdg_toplevel_close,
};

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

/* ── view callbacks ──────────────────────────────────────────────────────── */

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
    app_relayout_active();
    app_repaint_chrome();
}

static void dl_need_path_cb(const char *uri, const char *suggested, void *d)
{
    (void)d;
    Tab *t = app_active_tab();
    if (!t) return;
    g_free(g_app.dl_pending_uri);
    g_app.dl_pending_uri = g_strdup(uri);

    const char *dldir = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    if (!dldir) dldir = g_get_home_dir();
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

/* ── pointer state ───────────────────────────────────────────────────────── */

static struct wl_surface *ptr_surface;
static int ptr_x, ptr_y;
static uint32_t ptr_serial;

/* Check if a surface belongs to our chrome (tabbar, statusbar, dlbar, historybar) */
static gboolean is_chrome_surface(struct wl_surface *surf)
{
    if (!surf) return FALSE;
    if (g_app.tabbar && surf == g_app.tabbar->surface) return TRUE;
    if (g_app.statusbar && surf == g_app.statusbar->surface) return TRUE;
    if (g_app.dlbar && surf == g_app.dlbar->surface) return TRUE;
    if (g_app.historybar && surf == g_app.historybar->surface) return TRUE;
    return FALSE;
}

/* Forward a pointer event to the active WPE view */
static void forward_pointer_event(WPEEventType type, uint32_t time,
    guint button, WPEModifiers mods)
{
    Tab *t = app_active_tab();
    if (!t || !t->view) return;
    /* Coordinates relative to the view subsurface */
    double vx = ptr_x - g_app.view_x;
    double vy = ptr_y - g_app.view_y;

    WPEEvent *ev;
    if (type == WPE_EVENT_SCROLL) {
        /* Handled separately in ptr_axis */
        return;
    }
    if (type == WPE_EVENT_POINTER_MOVE) {
        ev = wpe_event_pointer_move_new(type, t->view,
            WPE_INPUT_SOURCE_MOUSE, time, mods,
            vx, vy, 0, 0);
    } else {
        /* POINTER_DOWN: press_count=1, POINTER_UP: press_count=0 */
        guint pc = (type == WPE_EVENT_POINTER_DOWN) ? 1 : 0;
        ev = wpe_event_pointer_button_new(type, t->view,
            WPE_INPUT_SOURCE_MOUSE, time, mods,
            button, vx, vy, pc);
    }
    wpe_view_event(t->view, ev);
    wpe_event_unref(ev);
}

static WPEModifiers get_pointer_mods(void)
{
    /* We don't track button state ourselves; return 0 for now.
     * WPE gets modifier info from its own keymap state. */
    return (WPEModifiers)0;
}

static void ptr_enter(void *d, struct wl_pointer *p, uint32_t ser,
    struct wl_surface *surf, wl_fixed_t x, wl_fixed_t y)
{
    (void)d;
    ptr_surface = surf;
    ptr_x = wl_fixed_to_int(x);
    ptr_y = wl_fixed_to_int(y);
    ptr_serial = ser;

    if (is_chrome_surface(surf)) {
        wayland_set_cursor(&g_app.wl, p, ser, "default");
    } else {
        wayland_set_cursor(&g_app.wl, p, ser, "default");
        Tab *t = app_active_tab();
        if (t && t->view) {
            double vx = ptr_x - g_app.view_x;
            double vy = ptr_y - g_app.view_y;
            WPEEvent *ev = wpe_event_pointer_move_new(
                WPE_EVENT_POINTER_ENTER, t->view,
                WPE_INPUT_SOURCE_MOUSE, 0, get_pointer_mods(),
                vx, vy, 0, 0);
            wpe_view_event(t->view, ev);
            wpe_event_unref(ev);
        }
    }
}

static void ptr_leave(void *d, struct wl_pointer *p, uint32_t ser,
    struct wl_surface *surf)
{
    (void)d; (void)p; (void)ser;
    if (!is_chrome_surface(surf)) {
        Tab *t = app_active_tab();
        if (t && t->view) {
            WPEEvent *ev = wpe_event_pointer_move_new(
                WPE_EVENT_POINTER_LEAVE, t->view,
                WPE_INPUT_SOURCE_MOUSE, 0, get_pointer_mods(),
                0, 0, 0, 0);
            wpe_view_event(t->view, ev);
            wpe_event_unref(ev);
        }
    }
    ptr_surface = NULL;
}

static void ptr_motion(void *d, struct wl_pointer *p, uint32_t t,
    wl_fixed_t x, wl_fixed_t y)
{
    (void)d; (void)p;
    ptr_x = wl_fixed_to_int(x);
    ptr_y = wl_fixed_to_int(y);

    if (!is_chrome_surface(ptr_surface)) {
        forward_pointer_event(WPE_EVENT_POINTER_MOVE, t, 0, get_pointer_mods());
    }
}

static void ptr_button(void *d, struct wl_pointer *p, uint32_t ser,
    uint32_t t, uint32_t btn, uint32_t state)
{
    (void)d; (void)p; (void)ser;

    /* Chrome clicks (tabbar) — only on press */
    if (state == WL_POINTER_BUTTON_STATE_PRESSED &&
        g_app.tabbar && ptr_surface == g_app.tabbar->surface) {
        int n = g_app.tabs.count;
        if (n > 0) {
            int tw = g_app.tabbar->width / n;
            if (tw > 200) tw = 200;
            int idx = ptr_x / tw;
            if (idx >= 0 && idx < n) {
                if (btn == BTN_LEFT) {
                    tabarray_switch(&g_app.tabs, idx);
                    app_repaint_chrome();
                    return;
                } else if (btn == BTN_MIDDLE) {
                    tab_close_cb(idx, NULL);
                    return;
                }
            }
        }
    }

    /* Forward to WPE */
    if (!is_chrome_surface(ptr_surface)) {
        WPEEventType type = (state == WL_POINTER_BUTTON_STATE_PRESSED)
            ? WPE_EVENT_POINTER_DOWN : WPE_EVENT_POINTER_UP;
        /* Map Linux input button codes to WPE button numbers */
        guint wpe_btn = 0;
        switch (btn) {
        case BTN_LEFT:   wpe_btn = 1; break;  /* WPE_BUTTON_PRIMARY */
        case BTN_MIDDLE: wpe_btn = 2; break;  /* WPE_BUTTON_MIDDLE */
        case BTN_RIGHT:  wpe_btn = 3; break;  /* WPE_BUTTON_SECONDARY */
        default:         wpe_btn = btn; break;
        }
        forward_pointer_event(type, t, wpe_btn, get_pointer_mods());
    }
}

static void ptr_axis(void *d, struct wl_pointer *p, uint32_t t,
    uint32_t axis, wl_fixed_t v)
{
    (void)d; (void)p;
    if (is_chrome_surface(ptr_surface)) return;

    Tab *tab = app_active_tab();
    if (!tab || !tab->view) return;

    double dx = 0, dy = 0;
    /* axis 0 = WL_POINTER_AXIS_VERTICAL_SCROLL, 1 = HORIZONTAL */
    if (axis == 0)
        dy = -wl_fixed_to_double(v);
    else
        dx = -wl_fixed_to_double(v);

    double vx = ptr_x - g_app.view_x;
    double vy = ptr_y - g_app.view_y;

    WPEEvent *ev = wpe_event_scroll_new(tab->view,
        WPE_INPUT_SOURCE_MOUSE, t, get_pointer_mods(),
        dx, dy, FALSE, FALSE, vx, vy);
    wpe_view_event(tab->view, ev);
    wpe_event_unref(ev);
}

static void ptr_frame(void *d, struct wl_pointer *p)
{ (void)d; (void)p; }

static void ptr_axis_source(void *d, struct wl_pointer *p, uint32_t s)
{ (void)d; (void)p; (void)s; }

static void ptr_axis_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a)
{ (void)d; (void)p; (void)t; (void)a; }

static void ptr_axis_discrete(void *d, struct wl_pointer *p, uint32_t a, int32_t dd)
{ (void)d; (void)p; (void)a; (void)dd; }

static const struct wl_pointer_listener pointer_listener = {
    .enter         = ptr_enter,
    .leave         = ptr_leave,
    .motion        = ptr_motion,
    .button        = ptr_button,
    .axis          = ptr_axis,
    .frame         = ptr_frame,
    .axis_source   = ptr_axis_source,
    .axis_stop     = ptr_axis_stop,
    .axis_discrete = ptr_axis_discrete,
};

/* ── keyboard ─────────────────────────────────────────────────────────────── */

static struct xkb_context  *xkb_ctx;
static struct xkb_keymap   *xkb_kmap;
static struct xkb_state    *xkb_st;
static WPEKeymapXKB        *wpe_kmap;

static WPEModifiers xkb_mods_to_wpe(struct xkb_state *state)
{
    WPEModifiers mods = (WPEModifiers)0;
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE))
        mods = (WPEModifiers)(mods | WPE_MODIFIER_KEYBOARD_CONTROL);
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE))
        mods = (WPEModifiers)(mods | WPE_MODIFIER_KEYBOARD_SHIFT);
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE))
        mods = (WPEModifiers)(mods | WPE_MODIFIER_KEYBOARD_ALT);
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE))
        mods = (WPEModifiers)(mods | WPE_MODIFIER_KEYBOARD_META);
    if (xkb_state_mod_name_is_active(state, XKB_LED_NAME_CAPS, XKB_STATE_MODS_EFFECTIVE))
        mods = (WPEModifiers)(mods | WPE_MODIFIER_KEYBOARD_CAPS_LOCK);
    return mods;
}

/* ── Key repeat ────────────────────────────────────────────────────────── */
static guint    repeat_keycode = 0;
static guint    repeat_time    = 0;
static guint    repeat_rate    = 40;   /* repeats/sec (default) */
static guint    repeat_delay   = 300;  /* ms before repeat starts */
static guint    repeat_delay_id  = 0;
static guint    repeat_interval_id = 0;

static gboolean key_repeat_fire(gpointer data)
{
    (void)data;
    Tab *t = app_active_tab();
    if (!t || !t->view || !xkb_st) return FALSE;

    WPEModifiers mods = xkb_mods_to_wpe(xkb_st);
    guint keyval = xkb_state_key_get_one_sym(xkb_st, repeat_keycode);
    repeat_time += 1000 / repeat_rate;

    WPEEvent *ev = wpe_event_keyboard_new(WPE_EVENT_KEYBOARD_KEY_DOWN, t->view,
        WPE_INPUT_SOURCE_KEYBOARD, repeat_time, mods, repeat_keycode, keyval);
    if (ev) {
        wpe_view_event(t->view, ev);
        wpe_event_unref(ev);
    }
    return TRUE;  /* keep going */
}

static gboolean key_repeat_delay_cb(gpointer data)
{
    (void)data;
    repeat_delay_id = 0;
    if (repeat_interval_id) g_source_remove(repeat_interval_id);
    repeat_interval_id = g_timeout_add(1000 / repeat_rate, key_repeat_fire, NULL);
    return FALSE;  /* one-shot */
}

static void key_repeat_stop(void)
{
    if (repeat_delay_id)    { g_source_remove(repeat_delay_id);    repeat_delay_id = 0; }
    if (repeat_interval_id) { g_source_remove(repeat_interval_id); repeat_interval_id = 0; }
    repeat_keycode = 0;
}

static void key_repeat_start(guint keycode, guint time_ms)
{
    key_repeat_stop();
    repeat_keycode = keycode;
    repeat_time    = time_ms;
    repeat_delay_id = g_timeout_add(repeat_delay, key_repeat_delay_cb, NULL);
}

static void kb_keymap(void *data, struct wl_keyboard *kb,
    uint32_t format, int32_t fd, uint32_t size)
{
    (void)data; (void)kb;

    /* Feed the keymap to WPE's XKB keymap (needs the fd) */
    if (wpe_kmap)
        wpe_keymap_xkb_update(wpe_kmap, format, fd, size);

    char *map_str = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (map_str == MAP_FAILED) return;

    struct xkb_keymap *new_keymap = xkb_keymap_new_from_string(
        xkb_ctx, map_str, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map_str, size);

    if (!new_keymap) return;
    if (xkb_kmap) xkb_keymap_unref(xkb_kmap);
    if (xkb_st) xkb_state_unref(xkb_st);
    xkb_kmap = new_keymap;
    xkb_st = xkb_state_new(xkb_kmap);
}

static void kb_enter(void *data, struct wl_keyboard *kb,
    uint32_t serial, struct wl_surface *surf, struct wl_array *keys)
{
    (void)data; (void)kb; (void)serial; (void)keys;
    if (!is_chrome_surface(surf)) {
        Tab *t = app_active_tab();
        if (t && t->view)
            wpe_view_focus_in(t->view);
    }
}

static void kb_leave(void *data, struct wl_keyboard *kb,
    uint32_t serial, struct wl_surface *surf)
{
    (void)data; (void)kb; (void)serial;
    if (!is_chrome_surface(surf)) {
        Tab *t = app_active_tab();
        if (t && t->view)
            wpe_view_focus_out(t->view);
    }
}

static void kb_key(void *data, struct wl_keyboard *kb,
    uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
    (void)data; (void)kb; (void)serial;
    Tab *t = app_active_tab();
    if (!t || !t->view) return;

    /* Lazily init XKB state from WPE's keymap if our keymap callback never fired */
    if (!xkb_st) {
        if (wpe_kmap) {
            struct xkb_keymap *kmap = wpe_keymap_xkb_get_xkb_keymap(wpe_kmap);
            if (kmap) {
                xkb_st = xkb_state_new(kmap);
            }
        }
        if (!xkb_st) {
            return;
        }
    }

    /* key is a Linux evdev scancode; xkb expects keycode = scancode + 8 */
    xkb_keycode_t keycode = key + 8;

    /* Update xkb state */
    enum xkb_key_direction direction = (state == WL_KEYBOARD_KEY_STATE_PRESSED)
        ? XKB_KEY_DOWN : XKB_KEY_UP;
    xkb_state_update_key(xkb_st, keycode, direction);

    WPEModifiers mods = xkb_mods_to_wpe(xkb_st);
    guint keyval = xkb_state_key_get_one_sym(xkb_st, keycode);

    WPEEventType ev_type = (state == WL_KEYBOARD_KEY_STATE_PRESSED)
        ? WPE_EVENT_KEYBOARD_KEY_DOWN : WPE_EVENT_KEYBOARD_KEY_UP;

    WPEEvent *ev = wpe_event_keyboard_new(ev_type, t->view,
        WPE_INPUT_SOURCE_KEYBOARD, time, mods, keycode, keyval);
    if (ev) {
        wpe_view_event(t->view, ev);
        wpe_event_unref(ev);
    }

    /* Key repeat: start timer on press, stop on release */
    if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
        key_repeat_start(keycode, time);
    else
        key_repeat_stop();
}

static void kb_modifiers(void *data, struct wl_keyboard *kb,
    uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
    uint32_t mods_locked, uint32_t group)
{
    (void)data; (void)kb; (void)serial;
    if (!xkb_st) return;
    xkb_state_update_mask(xkb_st, mods_depressed, mods_latched,
        mods_locked, 0, 0, group);
}

static void kb_repeat_info(void *data, struct wl_keyboard *kb,
    int32_t rate, int32_t delay)
{
    (void)data; (void)kb;
    if (rate > 0)  repeat_rate  = rate;
    if (delay > 0) repeat_delay = delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap      = kb_keymap,
    .enter       = kb_enter,
    .leave       = kb_leave,
    .key         = kb_key,
    .modifiers   = kb_modifiers,
    .repeat_info = kb_repeat_info,
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
    if (!home) home = "/tmp";
    snprintf(path, sizeof(path), "%s/.surf/crash.log", home);

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) fd = STDERR_FILENO;

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

    if (fd != STDERR_FILENO) close(fd);

    signal(sig, SIG_DFL);
    raise(sig);
}

/* ── FIFO ────────────────────────────────────────────────────────────────── */

static void
eval_on_active(const char *js)
{
    Tab *t = app_active_tab();
    if (t && js && *js)
        webkit_web_view_evaluate_javascript(t->wv, js, -1,
                                            NULL, NULL, NULL, NULL, NULL);
}

static gboolean
fifo_read(GIOChannel *chan, GIOCondition cond, gpointer data)
{
    (void)cond; (void)data;
    gchar *line = NULL;
    gsize len;
    GError *err = NULL;

    if (g_io_channel_read_line(chan, &line, &len, NULL, &err)
        != G_IO_STATUS_NORMAL) {
        if (err) g_error_free(err);
        return TRUE;
    }
    if (!line) return TRUE;
    g_strstrip(line);

    if (g_str_has_prefix(line, "open ")) {
        const char *url = line + 5;
        gboolean new_tab = FALSE;
        while (*url == '-') {
            if (g_str_has_prefix(url, "-t"))
                new_tab = TRUE;
            while (*url && *url != ' ') url++;
            while (*url == ' ') url++;
        }
        if (*url) {
            if (new_tab) {
                act_new_tab(NULL);
                Tab *t = app_active_tab();
                if (t) webkit_web_view_load_uri(t->wv, url);
            } else {
                Tab *t = app_active_tab();
                if (t) webkit_web_view_load_uri(t->wv, url);
            }
        }
    } else if (g_str_has_prefix(line, "jseval ")) {
        eval_on_active(line + 7);
    } else if (g_str_has_prefix(line, "message-error ")) {
        fprintf(stderr, "surf fifo: %s\n", line + 14);
    } else if (g_str_has_prefix(line, "message-info ")) {
        fprintf(stderr, "surf fifo: %s\n", line + 13);
    }

    g_free(line);
    return TRUE;
}

static void
setup_fifo(void)
{
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (!runtime_dir || !*runtime_dir)
        runtime_dir = "/tmp";

    g_app.fifo_path = g_strdup_printf("%s/surf-fifo-%d",
        runtime_dir, (int)getpid());
    unlink(g_app.fifo_path);

    if (mkfifo(g_app.fifo_path, 0600) != 0) {
        fprintf(stderr, "surf: mkfifo %s: %s\n",
            g_app.fifo_path, g_strerror(errno));
        g_free(g_app.fifo_path);
        g_app.fifo_path = NULL;
        return;
    }

    int fd = open(g_app.fifo_path, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "surf: open fifo: %s\n", g_strerror(errno));
        unlink(g_app.fifo_path);
        g_free(g_app.fifo_path);
        g_app.fifo_path = NULL;
        return;
    }

    g_app.fifo_chan = g_io_channel_unix_new(fd);
    g_io_channel_set_encoding(g_app.fifo_chan, NULL, NULL);
    g_io_channel_set_flags(g_app.fifo_chan,
        g_io_channel_get_flags(g_app.fifo_chan) | G_IO_FLAG_NONBLOCK, NULL);
    g_io_channel_set_close_on_unref(g_app.fifo_chan, TRUE);
    g_io_add_watch(g_app.fifo_chan, G_IO_IN, fifo_read, NULL);

    setenv("SURF_FIFO", g_app.fifo_path, 1);
}

/* ── Debug callbacks ──────────────────────────────────────────────────── */
static void debug_is_loading_cb(WebKitWebView *v, GParamSpec *, gpointer) {
    (void)v;
}
static void debug_progress_cb(WebKitWebView *v, GParamSpec *, gpointer) {
    (void)v;
}
static gboolean debug_load_failed_cb(WebKitWebView *v, WebKitLoadEvent e,
    const char *uri, GError *err, gpointer) {
    (void)v; (void)e; (void)uri; (void)err;
    return FALSE;
}
static void debug_web_process_terminated_cb(WebKitWebView *v,
    WebKitWebProcessTerminationReason r, gpointer) {
    (void)v; (void)r;
}

/* ── main — Path A bootstrap ───────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    const char *url = argc > 1 ? argv[1] : "about:blank";

    /* 1. Open our own Wayland connection and bind globals */
    wayland_connect(&g_app.wl);

    /* 2. Create our xdg_toplevel */
    g_app.root_surface = wl_compositor_create_surface(g_app.wl.compositor);
    g_app.xdg_surface  = xdg_wm_base_get_xdg_surface(g_app.wl.wm_base,
                                                      g_app.root_surface);
    g_app.xdg_toplevel = xdg_surface_get_toplevel(g_app.xdg_surface);
    xdg_toplevel_set_app_id(g_app.xdg_toplevel, "surf");
    xdg_toplevel_set_title(g_app.xdg_toplevel, "surf");

    /* 3. Wire configure listeners */
    xdg_wm_base_add_listener(g_app.wl.wm_base, &xdg_wm_base_listener, NULL);
    xdg_surface_add_listener(g_app.xdg_surface, &xdg_surface_listener, NULL);
    xdg_toplevel_add_listener(g_app.xdg_toplevel, &xdg_toplevel_listener, NULL);

    /* 4. First commit to trigger configure */
    wl_surface_commit(g_app.root_surface);

    /* 5. Roundtrip until configured */
    while (!g_app.configured)
        wl_display_dispatch(g_app.wl.display);

    /* 6. Create the WPE view subsurface (positioned by app_layout) */
    g_app.view_surface = wl_compositor_create_surface(g_app.wl.compositor);
    g_app.view_subsurface = wl_subcompositor_get_subsurface(
        g_app.wl.subcompositor, g_app.view_surface, g_app.root_surface);
    wl_subsurface_set_desync(g_app.view_subsurface);

    /* 7. Create our custom WPE platform (SurfDisplay) */
    g_app.sdisplay = surf_display_new(
        g_app.wl.display,
        g_app.wl.compositor,
        g_app.wl.subcompositor,
        g_app.wl.shm,
        g_app.wl.wm_base,
        g_app.wl.dmabuf);

    /* Connect the display — this sets up GLib main loop integration and
     * collects DMA-BUF formats */
    GError *err = NULL;
    if (!wpe_display_connect(WPE_DISPLAY(g_app.sdisplay), &err))
        g_error("surf: WPE display connect failed: %s", err->message);

    /* Set as primary so WebKit picks it up */
    wpe_display_set_primary(WPE_DISPLAY(g_app.sdisplay));

    /* 8. Create our toplevel (SurfToplevel) with unlimited views */
    g_app.toplevel = wpe_display_create_toplevel(WPE_DISPLAY(g_app.sdisplay), 0);

    /* 9. Initial layout with configure size */
    app_layout(g_app.window_w, g_app.window_h);

    /* 10. WebKit/Network session setup */
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

    /* 11. Initialize tab/download/history systems */
    tabarray_init(&g_app.tabs);
    downloads_init(&g_app.dls);
    history_state_init(&g_app.history);
    settings_init();
    g_app.tab_close_fn = tab_close_cb;

    /* 12. Create first tab — WebKit will call our SurfDisplay::create_view
     * which returns a SurfView. We then wire its wl_surface to our subsurface. */
    Tab *first = tabarray_new(&g_app.tabs, WPE_DISPLAY(g_app.sdisplay),
        g_app.toplevel, tab_changed_cb, tab_close_cb, NULL);

    /* Surface wiring already done inside tabarray_new via surf_view_set_wl_surface */

    g_signal_connect(first->view, "closed",
        G_CALLBACK(on_view_closed), NULL);

    /* Tell WPE the view size */
    wpe_view_resized(first->view, g_app.view_w, g_app.view_h);

    /* 13. Wire downloads */
    WebKitNetworkSession *ns = g_app.network_session
        ? g_app.network_session
        : webkit_web_view_get_network_session(first->wv);
    if (ns)
        downloads_attach_session(&g_app.dls, ns,
            dl_changed_cb, dl_need_path_cb, NULL);

    /* 14. Wire pointer for tab clicks */
    if (g_app.wl.pointer)
        wl_pointer_add_listener(g_app.wl.pointer, &pointer_listener, NULL);

    /* 14b. Wire keyboard + XKB keymap */
    xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (xkb_ctx) {
        wpe_kmap = WPE_KEYMAP_XKB(wpe_keymap_xkb_new());
        /* Install the WPE keymap on our display */
        if (g_app.sdisplay) {
            /* The SurfDisplay's get_keymap vfunc returns whatever we set;
               store it so the display class can return it. */
            /* We'll set it via a display-level function if available,
               or just keep it referenced for the display's get_keymap. */
        }
    }
    if (g_app.wl.keyboard) {
        wl_keyboard_add_listener(g_app.wl.keyboard, &keyboard_listener, NULL);
        /* Dispatch the keymap event from the compositor before entering main loop */
        wl_display_roundtrip(g_app.wl.display);
    } else

    /* 14c. Tell WPE we have mouse + keyboard */
    if (g_app.sdisplay) {
        WPEDisplay *dpy = WPE_DISPLAY(g_app.sdisplay);
        wpe_display_set_available_input_devices(dpy,
            (WPEAvailableInputDevices)(
                WPE_AVAILABLE_INPUT_DEVICE_MOUSE |
                WPE_AVAILABLE_INPUT_DEVICE_KEYBOARD));
    }

    /* 15. Input handling */
    input_init(&in, first->view, NULL, NULL);

    /* 16. FIFO, load URL, crash handler */
    setup_fifo();

    g_signal_connect(first->wv, "notify::is-loading",
        G_CALLBACK(debug_is_loading_cb), NULL);
    g_signal_connect(first->wv, "notify::estimated-load-progress",
        G_CALLBACK(debug_progress_cb), NULL);
    g_signal_connect(first->wv, "load-failed",
        G_CALLBACK(debug_load_failed_cb), NULL);
    g_signal_connect(first->wv, "web-process-terminated",
        G_CALLBACK(debug_web_process_terminated_cb), NULL);

    webkit_web_view_load_uri(first->wv, url);

    struct sigaction sa = {0};
    sa.sa_sigaction = crashhandler;
    sa.sa_flags     = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);

    /* 17. Enter main loop */
    g_app.loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(g_app.loop);

    /* ── cleanup ─────────────────────────────────────────────────────────── */

    /* Destroy chrome panels */
    if (g_app.tabbar)    chrome_panel_destroy(g_app.tabbar);
    if (g_app.historybar) chrome_panel_destroy(g_app.historybar);
    if (g_app.statusbar) chrome_panel_destroy(g_app.statusbar);
    if (g_app.dlbar)     chrome_panel_destroy(g_app.dlbar);

    /* Destroy view subsurface */
    if (g_app.view_subsurface) wl_subsurface_destroy(g_app.view_subsurface);
    if (g_app.view_surface)    wl_surface_destroy(g_app.view_surface);

    /* Destroy xdg_toplevel */
    if (g_app.root_buffer)     wl_buffer_destroy(g_app.root_buffer);
    if (g_app.xdg_toplevel)    xdg_toplevel_destroy(g_app.xdg_toplevel);
    if (g_app.xdg_surface)     xdg_surface_destroy(g_app.xdg_surface);
    if (g_app.root_surface)    wl_surface_destroy(g_app.root_surface);

    /* Tear down tabs (unrefs WebKit views) before destroying WPE display */
    tabarray_free(&g_app.tabs);

    if (g_app.toplevel)     g_object_unref(g_app.toplevel);
    if (g_app.sdisplay)     g_object_unref(g_app.sdisplay);

    wayland_finish(&g_app.wl);

    if (g_app.fifo_chan) g_io_channel_unref(g_app.fifo_chan);
    if (g_app.fifo_path) { unlink(g_app.fifo_path); g_free(g_app.fifo_path); }

    downloads_free(&g_app.dls);
    history_state_free(&g_app.history);
    g_free(g_app.dl_pending_uri);
    if (g_app.network_session &&
        g_app.network_session != webkit_network_session_get_default())
        g_object_unref(g_app.network_session);
    g_main_loop_unref(g_app.loop);
    return 0;
}
