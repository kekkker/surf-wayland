#pragma once

#include "wayland.h"
#include "chrome.h"
#include "cmdbar.h"
#include "tabs.h"
#include "download.h"
#include "history.h"
#include <glib.h>

typedef struct {
    WaylandState       wl;
    WPEDisplayWayland *display;
    WPEToplevel       *toplevel;
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
    int                window_w;
    int                window_h;
    /* Chrome container: intermediate wl_surface whose subsurfaces are all
     * chrome panels.  We commit this surface (not the toplevel) so we
     * never interfere with WPE's own toplevel commits for view management. */
    struct wl_surface    *chrome_bg;
    struct wl_subsurface *chrome_bg_sub;
    HistoryState       history;
    HistoryMatch       history_matches[HISTORY_MAX_MATCHES];
    int                history_match_count;
    int                history_match_selected;
} AppState;

extern AppState g_app;

/* Convenience: active tab, or NULL */
static inline Tab *app_active_tab(void)
{
    return tabarray_active(&g_app.tabs);
}

void app_repaint_chrome(void);
void app_layout_chrome(int w, int h);
void app_relayout_active(void);
void app_raise_chrome(void);
void app_cmdbar_refresh_history(void);
void app_cmdbar_clear_history(void);
gboolean app_cmdbar_select_history(int direction);
