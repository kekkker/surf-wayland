#include "download.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

/* ── pending-path bridge from main.c ────────────────────────────────────── */

extern char *g_dl_pending_path;     /* defined in main.c */

/* ── static helpers ──────────────────────────────────────────────────────── */

static DownloadList *g_dl_list;     /* set by downloads_attach_session */
static DLChangedFn   g_dl_changed;
static DLNeedPathFn  g_dl_need_path;
static void         *g_dl_cb_data;

static char *fmt_size(guint64 bytes)
{
    if (bytes >= (guint64)1024 * 1024 * 1024)
        return g_strdup_printf("%.2fGB", bytes / (1024.0 * 1024.0 * 1024.0));
    if (bytes >= 1024 * 1024)
        return g_strdup_printf("%.2fMB", bytes / (1024.0 * 1024.0));
    if (bytes >= 1024)
        return g_strdup_printf("%.2fKB", bytes / 1024.0);
    return g_strdup_printf("%" G_GUINT64_FORMAT "B", bytes);
}

static Download *find_by_dl(DownloadList *list, WebKitDownload *d)
{
    for (int i = 0; i < list->count; i++)
        if (list->items[i].dl == d) return &list->items[i];
    return NULL;
}

static Download *list_append(DownloadList *list)
{
    if (list->count == list->cap) {
        list->cap = list->cap ? list->cap * 2 : 4;
        list->items = g_realloc(list->items, list->cap * sizeof *list->items);
    }
    Download *d = &list->items[list->count++];
    memset(d, 0, sizeof *d);
    return d;
}

/* ── per-download signals ────────────────────────────────────────────────── */

static void on_progress(GObject *o, GParamSpec *p, gpointer ud)
{
    (void)p; (void)ud;
    WebKitDownload *d = WEBKIT_DOWNLOAD(o);
    Download *rec = find_by_dl(g_dl_list, d);
    if (!rec) return;

    double elapsed = webkit_download_get_elapsed_time(d);
    guint64 recv   = webkit_download_get_received_data_length(d);

    double dt = elapsed - rec->prev_time;
    if (dt >= 0.5 && !rec->done) {
        double inst = (double)(recv - rec->prev_recv) / dt;
        rec->speed  = (rec->prev_time == 0.0) ? inst
                                              : rec->speed * 0.6 + inst * 0.4;
        rec->prev_recv = recv;
        rec->prev_time = elapsed;
    }
    if (g_dl_changed) g_dl_changed(g_dl_cb_data);
}

static void on_finished(WebKitDownload *d, gpointer ud)
{
    (void)ud;
    Download *rec = find_by_dl(g_dl_list, d);
    if (!rec) return;
    rec->done = 1;
    if (g_dl_changed) g_dl_changed(g_dl_cb_data);
}

static void on_failed(WebKitDownload *d, GError *err, gpointer ud)
{
    (void)err; (void)ud;
    Download *rec = find_by_dl(g_dl_list, d);
    if (!rec) return;
    rec->done   = 1;
    rec->failed = 1;
    if (g_dl_changed) g_dl_changed(g_dl_cb_data);
}

static gboolean on_decide_destination(WebKitDownload *d,
    gchar *suggested_filename, gpointer ud)
{
    (void)ud;

    /* Second pass: user already confirmed a path → set it and record. */
    if (g_dl_pending_path) {
        webkit_download_set_destination(d, g_dl_pending_path);

        Download *rec = list_append(g_dl_list);
        rec->dl    = d;
        rec->index = ++g_dl_list->counter;
        rec->name  = g_path_get_basename(g_dl_pending_path);
        WebKitURIResponse *resp = webkit_download_get_response(d);
        rec->total = resp ? webkit_uri_response_get_content_length(resp) : -1;

        g_signal_connect(d, "notify::estimated-progress",
            G_CALLBACK(on_progress), NULL);
        g_signal_connect(d, "finished", G_CALLBACK(on_finished), NULL);
        g_signal_connect(d, "failed",   G_CALLBACK(on_failed),   NULL);

        g_free(g_dl_pending_path);
        g_dl_pending_path = NULL;

        if (g_dl_changed) g_dl_changed(g_dl_cb_data);
        return TRUE;
    }

    /* First pass: stash URI + suggested name, prompt the user, cancel. */
    if (g_dl_need_path) {
        WebKitURIRequest *req = webkit_download_get_request(d);
        const char *uri = req ? webkit_uri_request_get_uri(req) : NULL;
        const char *suggested = (suggested_filename && *suggested_filename)
            ? suggested_filename : "download";
        g_dl_need_path(uri, suggested, g_dl_cb_data);
    }
    webkit_download_cancel(d);
    return TRUE;
}

static void on_download_started(WebKitNetworkSession *ns, WebKitDownload *d,
    gpointer ud)
{
    (void)ns;
    g_signal_connect(d, "decide-destination",
        G_CALLBACK(on_decide_destination), ud);
}

/* ── public ──────────────────────────────────────────────────────────────── */

void downloads_init(DownloadList *dl)
{
    memset(dl, 0, sizeof *dl);
}

void downloads_free(DownloadList *dl)
{
    downloads_clear(dl);
    g_free(dl->items);
    dl->items = NULL;
    dl->cap   = 0;
}

void downloads_clear(DownloadList *dl)
{
    for (int i = 0; i < dl->count; i++)
        g_free(dl->items[i].name);
    dl->count = 0;
}

void downloads_attach_session(DownloadList *dl, WebKitNetworkSession *ns,
    DLChangedFn on_change, DLNeedPathFn on_need_path, void *cb_data)
{
    g_dl_list      = dl;
    g_dl_changed   = on_change;
    g_dl_need_path = on_need_path;
    g_dl_cb_data   = cb_data;
    g_signal_connect(ns, "download-started",
        G_CALLBACK(on_download_started), NULL);
}

void downloads_start_with_path(WebKitWebView *wv, const char *uri,
    const char *path)
{
    /* g_dl_pending_path is read by on_decide_destination to set the
     * destination and create the bookkeeping record. */
    g_dl_pending_path = g_strdup(path);
    webkit_web_view_download_uri(wv, uri);
}

char *download_format_line(const Download *d)
{
    const char *name = d->name ? d->name : "?";
    if (d->done) {
        if (d->failed)
            return g_strdup_printf("%u: %s [failed]", d->index, name);
        if (d->total > 0) {
            char *t = fmt_size((guint64)d->total);
            char *s = g_strdup_printf("%u: %s [done|%s]", d->index, name, t);
            g_free(t);
            return s;
        }
        return g_strdup_printf("%u: %s [done]", d->index, name);
    }
    guint64 recv = webkit_download_get_received_data_length(d->dl);
    double  pct  = webkit_download_get_estimated_progress(d->dl) * 100.0;
    char *r = fmt_size(recv);
    char *s;
    if (d->speed > 0 && d->total > 0) {
        char *spd = fmt_size((guint64)d->speed);
        char *tot = fmt_size((guint64)d->total);
        s = g_strdup_printf("%u: %s [%s/s|%.0f%%|%s/%s]",
            d->index, name, spd, pct, r, tot);
        g_free(spd); g_free(tot);
    } else if (d->total > 0) {
        char *tot = fmt_size((guint64)d->total);
        s = g_strdup_printf("%u: %s [%.0f%%|%s/%s]",
            d->index, name, pct, r, tot);
        g_free(tot);
    } else {
        s = g_strdup_printf("%u: %s [%s]", d->index, name, r);
    }
    g_free(r);
    return s;
}
