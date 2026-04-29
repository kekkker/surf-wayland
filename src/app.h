#pragma once

#include "wayland.h"
#include "chrome.h"
#include "cmdbar.h"
#include "tabs.h"
#include <glib.h>

typedef struct {
    WaylandState       wl;
    WPEDisplayWayland *display;
    WPEToplevel       *toplevel;
    TabArray           tabs;
    ChromePanel       *tabbar;
    ChromePanel       *statusbar;
    CmdBar             cmdbar;
    TabCloseFn         tab_close_fn;
    GMainLoop         *loop;
    int                fullscreen;
} AppState;

extern AppState g_app;

/* Convenience: active tab, or NULL */
static inline Tab *app_active_tab(void)
{
    return tabarray_active(&g_app.tabs);
}

void app_repaint_chrome(void);
void app_layout_chrome(int w, int h);
