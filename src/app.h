#pragma once

#include "wayland.h"
#include "chrome.h"
#include "cmdbar.h"
#include "tabs.h"
#include "download.h"
#include "history.h"
#include <glib.h>
#include <wpe/wpe-platform.h>

/* Forward declarations for our custom WPE platform */
typedef struct _SurfDisplay SurfDisplay;
typedef struct _SurfToplevel SurfToplevel;

typedef struct {
    WaylandState       wl;

    /* Our own xdg_toplevel — NOT WPE's */
    struct wl_surface    *root_surface;
    struct xdg_surface   *xdg_surface;
    struct xdg_toplevel  *xdg_toplevel;
    struct wl_buffer     *root_buffer;
    int                   root_buf_w;
    int                   root_buf_h;
    int                   window_w, window_h;  /* current configure size */
    int                   pending_w, pending_h;/* from configure, applied on commit */
    gboolean              configured;          /* first configure received */

    /* Custom WPE platform */
    SurfDisplay         *sdisplay;            /* our WPEDisplay subclass */
    WPEToplevel         *toplevel;            /* SurfToplevel */

    /* WPE view subsurface — rendered by our custom platform */
    struct wl_surface    *view_surface;
    struct wl_subsurface *view_subsurface;
    int                   view_x, view_y;
    int                   view_w, view_h;

    TabArray           tabs;
    ChromePanel       *tabbar;
    ChromePanel       *statusbar;
    ChromePanel       *historybar;
    ChromePanel       *dlbar;        /* below tabbar; created on demand */
    CmdBar             cmdbar;
    TabCloseFn         tab_close_fn;
    GMainLoop         *loop;
    int                fullscreen;
    DownloadList       dls;
    char              *dl_pending_uri;  /* URI awaiting path confirmation */
    int                cookie_policy;   /* WebKitCookieAcceptPolicy */
    WebKitNetworkSession *network_session;
    HistoryState       history;
    HistoryMatch       history_matches[HISTORY_MAX_MATCHES];
    int                history_match_count;
    int                history_match_selected;
    char              *closed_tabs[32]; /* LIFO stack of closed tab URIs */
    int                closed_tab_top;
    char              *fifo_path;       /* $XDG_RUNTIME_DIR/surf-fifo-$pid */
    GIOChannel        *fifo_chan;
} AppState;

extern AppState g_app;
extern char *g_dl_pending_path;

/* Convenience: active tab, or NULL */
static inline Tab *app_active_tab(void)
{
    return tabarray_active(&g_app.tabs);
}

void app_repaint_chrome(void);
void app_layout(int w, int h);
void app_relayout_active(void);
void app_cmdbar_refresh_history(void);
void app_cmdbar_clear_history(void);
gboolean app_cmdbar_select_history(int direction);
void app_raise_chrome(void);
void app_layout_chrome(int win_w, int win_h);
