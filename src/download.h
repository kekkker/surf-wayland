#pragma once

#include <wpe/webkit.h>
#include <glib.h>

typedef struct {
    WebKitDownload *dl;
    guint           index;
    char           *name;
    gint64          total;       /* -1 if unknown */
    guint64         prev_recv;
    double          prev_time;
    double          speed;       /* smoothed bytes/sec */
    int             done;
    int             failed;
} Download;

typedef struct {
    Download *items;
    int       count;
    int       cap;
    guint     counter;           /* monotonic display index */
} DownloadList;

void downloads_init(DownloadList *dl);
void downloads_free(DownloadList *dl);
void downloads_clear(DownloadList *dl);

/* Connect "download-started" on a NetworkSession.
 *  on_change   : fires when a download appears, progresses, finishes, fails
 *  on_need_path: fires once per new download — caller should open path prompt
 *                and later call downloads_start_with_path(). */
typedef void (*DLChangedFn)(void *data);
typedef void (*DLNeedPathFn)(const char *uri, const char *suggested,
    void *data);

void downloads_attach_session(DownloadList *dl, WebKitNetworkSession *ns,
    DLChangedFn on_change, DLNeedPathFn on_need_path, void *cb_data);

/* Called from the path-prompt commit (cmdbar download mode). The pending URI
 * is consumed; a new download is started against the chosen path. */
void downloads_start_with_path(WebKitWebView *wv, const char *uri,
    const char *path);

/* Format a single line for the dlbar paint pass. Returns malloced string
 * (caller frees). */
char *download_format_line(const Download *d);
