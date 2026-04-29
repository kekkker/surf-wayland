#pragma once

#include <wpe/webkit.h>
#include <wpe/wpe-platform.h>
#include <glib.h>

typedef enum {
    MODE_NORMAL,
    MODE_INSERT,
    MODE_COMMAND,
    MODE_SEARCH,
    MODE_HINT,
} TabMode;

typedef struct Tab Tab;
struct Tab {
    WebKitWebView  *wv;
    WPEView        *view;

    /* page state */
    char           *title;
    char           *uri;
    int             progress;   /* 0-100 */
    int             https;
    int             insecure;
    int             pinned;
    TabMode         mode;

    /* find */
    WebKitFindController *finder;
    int             find_count;
    int             find_cur;

    /* hover */
    char           *hover_uri;
};

typedef struct {
    Tab    *items;
    int     count;
    int     active;
} TabArray;

typedef void (*TabChangedFn)(void *data);
typedef void (*TabCloseFn)(int idx, void *data);

void tabarray_init(TabArray *ta);
Tab *tabarray_new(TabArray *ta, WPEDisplay *display, WPEToplevel *toplevel,
    TabChangedFn on_change, TabCloseFn on_close, void *cb_data);
void tabarray_close(TabArray *ta, int index,
    TabChangedFn on_change, void *cb_data);
void tabarray_switch(TabArray *ta, int index);
void tabarray_free(TabArray *ta);

static inline Tab *tabarray_active(TabArray *ta)
{
    if (ta->count == 0) return NULL;
    return &ta->items[ta->active];
}
