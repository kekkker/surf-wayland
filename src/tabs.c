#include "tabs.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── per-tab signal data ─────────────────────────────────────────────────── */

typedef struct {
    TabArray     *ta;
    WebKitWebView *wv;   /* stable GObject ref — used to find the Tab */
    TabChangedFn  on_change;
    void         *cb_data;
} TabCBData;

/* Find Tab by wv — safe across realloc because wv pointer is stable. */
static Tab *find_tab(TabCBData *d)
{
    for (int i = 0; i < d->ta->count; i++)
        if (d->ta->items[i].wv == d->wv)
            return &d->ta->items[i];
    return NULL;
}

static void on_load_changed(WebKitWebView *wv, WebKitLoadEvent ev, gpointer ud)
{
    TabCBData *d = ud;
    Tab *t = find_tab(d);
    if (!t) return;
    if (ev == WEBKIT_LOAD_STARTED) {
        t->progress = 0;
        t->https    = 0;
        t->insecure = 0;
        g_clear_pointer(&t->title, g_free);
    }
    g_free(t->uri);
    t->uri = g_strdup(webkit_web_view_get_uri(wv));
    d->on_change(d->cb_data);
}

static void on_notify_progress(GObject *obj, GParamSpec *p, gpointer ud)
{
    (void)p;
    TabCBData *d = ud;
    Tab *t = find_tab(d);
    if (!t) return;
    t->progress = (int)(
        webkit_web_view_get_estimated_load_progress(WEBKIT_WEB_VIEW(obj)) * 100);
    d->on_change(d->cb_data);
}

static void on_notify_title(GObject *obj, GParamSpec *p, gpointer ud)
{
    (void)p;
    TabCBData *d = ud;
    Tab *t = find_tab(d);
    if (!t) return;
    g_free(t->title);
    t->title = g_strdup(webkit_web_view_get_title(WEBKIT_WEB_VIEW(obj)));
    d->on_change(d->cb_data);
}

static void on_insecure_content(WebKitWebView *wv,
    WebKitInsecureContentEvent e, gpointer ud)
{
    (void)wv; (void)e;
    TabCBData *d = ud;
    Tab *t = find_tab(d);
    if (t) { t->insecure = 1; d->on_change(d->cb_data); }
}

/* ── public ──────────────────────────────────────────────────────────────── */

void tabarray_init(TabArray *ta)
{
    ta->items  = NULL;
    ta->count  = 0;
    ta->active = -1;
}

Tab *tabarray_new(TabArray *ta, WPEDisplay *display, WPEToplevel *toplevel,
    TabChangedFn on_change, void *cb_data)
{
    ta->count++;
    ta->items = realloc(ta->items, ta->count * sizeof(Tab));
    int idx = ta->count - 1;
    Tab *t = &ta->items[idx];
    memset(t, 0, sizeof *t);
    t->progress = 100;
    t->mode     = MODE_NORMAL;

    t->wv = g_object_new(WEBKIT_TYPE_WEB_VIEW, "display", display, NULL);
    t->view = webkit_web_view_get_wpe_view(t->wv);

    /* Assign to our toplevel so all tabs share one OS window */
    if (toplevel)
        wpe_view_set_toplevel(t->view, toplevel);

    t->finder = webkit_web_view_get_find_controller(t->wv);

    TabCBData *cbd = g_new(TabCBData, 1);
    cbd->ta        = ta;
    cbd->wv        = t->wv;
    cbd->on_change = on_change;
    cbd->cb_data   = cb_data;

    g_signal_connect(t->wv, "load-changed",
        G_CALLBACK(on_load_changed), cbd);
    g_signal_connect(t->wv, "notify::estimated-load-progress",
        G_CALLBACK(on_notify_progress), cbd);
    g_signal_connect(t->wv, "notify::title",
        G_CALLBACK(on_notify_title), cbd);
    g_signal_connect(t->wv, "insecure-content-detected",
        G_CALLBACK(on_insecure_content), cbd);

    /* Unmap previous active */
    if (ta->active >= 0)
        wpe_view_unmap(ta->items[ta->active].view);

    ta->active = idx;
    wpe_view_map(t->view);
    wpe_view_focus_in(t->view);

    return t;
}

void tabarray_close(TabArray *ta, int idx,
    TabChangedFn on_change, void *cb_data)
{
    if (ta->count == 0 || idx < 0 || idx >= ta->count) return;

    Tab *t = &ta->items[idx];
    wpe_view_unmap(t->view);
    g_object_unref(t->wv);
    g_free(t->title);
    g_free(t->uri);

    /* Shift array */
    memmove(&ta->items[idx], &ta->items[idx + 1],
        (ta->count - idx - 1) * sizeof(Tab));
    ta->count--;

    if (ta->count == 0) {
        ta->active = -1;
        return;
    }

    int new_active = idx < ta->count ? idx : ta->count - 1;
    ta->active = new_active;
    wpe_view_map(ta->items[new_active].view);
    wpe_view_focus_in(ta->items[new_active].view);
    on_change(cb_data);
}

void tabarray_switch(TabArray *ta, int idx)
{
    if (idx < 0 || idx >= ta->count || idx == ta->active) return;

    wpe_view_unmap(ta->items[ta->active].view);
    ta->active = idx;
    wpe_view_map(ta->items[idx].view);
    wpe_view_focus_in(ta->items[idx].view);
}

void tabarray_free(TabArray *ta)
{
    for (int i = 0; i < ta->count; i++) {
        g_object_unref(ta->items[i].wv);
        g_free(ta->items[i].title);
        g_free(ta->items[i].uri);
    }
    free(ta->items);
    ta->items = NULL;
    ta->count = 0;
    ta->active = -1;
}
