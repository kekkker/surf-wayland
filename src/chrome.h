#pragma once

#include "wayland.h"
#include "cmdbar.h"
#include "history.h"
#include <cairo/cairo.h>
#include <wayland-client.h>

/* Heights in logical pixels */
#define CHROME_TABBAR_H   20
#define CHROME_STATUSBAR_H 22
#define CHROME_CMDROW_H   22
#define CHROME_DLROW_H    18
#define CHROME_DLBAR_MAX_ROWS 5

typedef struct {
    struct wl_surface    *surface;
    struct wl_subsurface *subsurface;
    struct wl_shm_pool   *pool;
    struct wl_buffer     *buffer;
    void                 *data;
    cairo_surface_t      *csurf;
    int                   width;
    int                   height;
    int                   fd;
} ChromePanel;

/* Tab info passed to tabbar paint */
typedef struct {
    const char *title;
    int         active;
    int         pinned;
} ChromeTab;

ChromePanel *chrome_panel_create(WaylandState *wl,
    struct wl_surface *parent, int x, int y, int w, int h);
void chrome_panel_resize(ChromePanel *p, WaylandState *wl, int w, int h);
void chrome_panel_set_position(ChromePanel *p, int x, int y);
void chrome_panel_destroy(ChromePanel *p);

void chrome_paint_tabbar(ChromePanel *p, ChromeTab *tabs, int n);
void chrome_paint_statusbar(ChromePanel *p, const char *text, int progress,
    int https, int insecure, const char *mode, int find_cur, int find_total);
void chrome_paint_cmdbar(ChromePanel *p, const CmdBar *cb,
    const HistoryMatch *matches, int count, int selected);
void chrome_paint_history(ChromePanel *p, const HistoryMatch *matches,
    int count, int selected);
void chrome_paint_dlbar(ChromePanel *p, char **lines, int nlines);
void chrome_panel_commit(ChromePanel *p);
