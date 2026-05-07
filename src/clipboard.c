/* Wayland regular-clipboard plumbing (wl_data_device).
 *
 * Owns:
 *  - wl_data_device + listener tracking the current selection offer
 *  - the most recent wl_data_offer from the compositor (for paste)
 *  - the most recently published wl_data_source (for copy) and its text
 *
 * Threading: single-threaded with the rest of the Wayland event loop.
 * No GLib mainloop integration here — copy `send` writes happen
 * synchronously when the compositor delivers wl_data_source.send.
 */

#include "clipboard.h"
#include "wayland.h"
#include "wlplatform/clipboard.h"

#include <wpe/wpe-platform.h>

#include <glib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/select.h>

/* MIME types we accept/offer. Order = preference: utf-8 first. */
#define MIME_TEXT_UTF8   "text/plain;charset=utf-8"
#define MIME_TEXT_PLAIN  "text/plain"
#define MIME_UTF8_STRING "UTF8_STRING"

static struct {
    WaylandState         *wl;
    uint32_t              serial;          /* most recent input event serial */

    /* Incoming: latest selection offer the compositor handed us. */
    struct wl_data_offer *current_offer;
    bool                  current_has_text;
    /* All MIME types advertised by current_offer (NULL-terminated array
     * of g_strdup'd strings, freed when current_offer is replaced). */
    GPtrArray            *current_formats;

    /* While the compositor is announcing offers via data_offer events, we
     * track the *pending* offer (not yet bound to selection). */
    struct wl_data_offer *pending_offer;
    bool                  pending_has_text;
    GPtrArray            *pending_formats;

    /* Outgoing: source we published, and the text it serves. */
    struct wl_data_source *our_source;
    char                  *our_text;

    /* Bridge to WPEClipboard so WebKit's read path sees external offers. */
    SurfClipboard        *wpe_clipboard;
} g;

static void
push_formats_to_wpe(void)
{
    if (!g.wpe_clipboard) return;
    if (!g.current_formats || g.current_formats->len <= 1) {
        surf_clipboard_remote_changed(g.wpe_clipboard, NULL);
        return;
    }
    /* current_formats is a NULL-terminated GPtrArray. Pass pdata. */
    surf_clipboard_remote_changed(g.wpe_clipboard,
        (const char * const *)g.current_formats->pdata);
}

/* ── wl_data_offer listener ─────────────────────────────────────────── */

static void
offer_offer(void *data, struct wl_data_offer *offer, const char *mime)
{
    (void)data; (void)offer;
    /* This fires for the *pending* offer — between data_device.data_offer
     * and data_device.selection. Track every MIME type so WebKit can
     * read non-text payloads (custom data, html, etc.) too. */
    if (!mime) return;
    if (strcmp(mime, MIME_TEXT_UTF8)   == 0 ||
        strcmp(mime, MIME_UTF8_STRING) == 0 ||
        strcmp(mime, MIME_TEXT_PLAIN)  == 0)
        g.pending_has_text = true;
    if (g.pending_formats)
        g_ptr_array_add(g.pending_formats, g_strdup(mime));
}

static void offer_source_actions(void *d, struct wl_data_offer *o, uint32_t a)
{ (void)d; (void)o; (void)a; }
static void offer_action(void *d, struct wl_data_offer *o, uint32_t a)
{ (void)d; (void)o; (void)a; }

static const struct wl_data_offer_listener offer_listener = {
    .offer          = offer_offer,
    .source_actions = offer_source_actions,
    .action         = offer_action,
};

/* ── wl_data_device listener ────────────────────────────────────────── */

static void
device_data_offer(void *data, struct wl_data_device *dev,
    struct wl_data_offer *offer)
{
    (void)data; (void)dev;
    /* New offer announced. Subsequent offer.offer events list its MIME
     * types, then either device.selection or device.drop_performed binds
     * it to a role. We only care about selection. */
    g.pending_offer = offer;
    g.pending_has_text = false;
    if (g.pending_formats) g_ptr_array_unref(g.pending_formats);
    g.pending_formats = g_ptr_array_new_with_free_func(g_free);
    wl_data_offer_add_listener(offer, &offer_listener, NULL);
}

static void
device_selection(void *data, struct wl_data_device *dev,
    struct wl_data_offer *offer)
{
    (void)data; (void)dev;
    /* Selection changed. `offer` is one previously announced via
     * data_offer (or NULL = cleared). */
    if (g.current_offer && g.current_offer != offer)
        wl_data_offer_destroy(g.current_offer);
    if (g.current_formats) {
        g_ptr_array_unref(g.current_formats);
        g.current_formats = NULL;
    }

    g.current_offer    = offer;
    g.current_has_text = (offer == g.pending_offer) ? g.pending_has_text : false;

    if (offer && offer == g.pending_offer && g.pending_formats) {
        /* NUL-terminate so we can pass pdata to WebKit. */
        g_ptr_array_add(g.pending_formats, NULL);
        g.current_formats = g.pending_formats;
        g.pending_formats = NULL;
    } else if (g.pending_formats) {
        g_ptr_array_unref(g.pending_formats);
        g.pending_formats = NULL;
    }

    /* Pending state is consumed. */
    g.pending_offer    = NULL;
    g.pending_has_text = false;

    push_formats_to_wpe();
}

/* DnD events — we don't drag, but listener struct requires them. */
static void device_enter(void *d, struct wl_data_device *dev, uint32_t s,
    struct wl_surface *surf, wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *o)
{ (void)d; (void)dev; (void)s; (void)surf; (void)x; (void)y; (void)o; }
static void device_leave(void *d, struct wl_data_device *dev)
{ (void)d; (void)dev; }
static void device_motion(void *d, struct wl_data_device *dev, uint32_t t,
    wl_fixed_t x, wl_fixed_t y)
{ (void)d; (void)dev; (void)t; (void)x; (void)y; }
static void device_drop(void *d, struct wl_data_device *dev)
{ (void)d; (void)dev; }

static const struct wl_data_device_listener device_listener = {
    .data_offer       = device_data_offer,
    .enter            = device_enter,
    .leave            = device_leave,
    .motion           = device_motion,
    .drop             = device_drop,
    .selection        = device_selection,
};

/* ── wl_data_source listener (we own outgoing) ──────────────────────── */

static void
source_target(void *data, struct wl_data_source *src, const char *mime)
{ (void)data; (void)src; (void)mime; }

static void
source_send(void *data, struct wl_data_source *src,
    const char *mime, int32_t fd)
{
    (void)data;
    /* Compositor asks us to write our text into `fd` for a paste consumer. */
    if (src != g.our_source || !g.our_text) {
        close(fd);
        return;
    }

    /* Ignore SIGPIPE for this write — consumer may close early. */
    void (*old)(int) = signal(SIGPIPE, SIG_IGN);

    /* Mark fd non-blocking with select fallback to avoid hanging the
     * Wayland thread on a stuck consumer. */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    size_t total = strlen(g.our_text);
    size_t off   = 0;
    while (off < total) {
        ssize_t n = write(fd, g.our_text + off, total - off);
        if (n > 0) { off += (size_t)n; continue; }
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
            fd_set wfds;
            FD_ZERO(&wfds); FD_SET(fd, &wfds);
            struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
            if (select(fd + 1, NULL, &wfds, NULL, &tv) <= 0) break;
            continue;
        }
        break;  /* EPIPE / other → bail */
    }
    close(fd);
    signal(SIGPIPE, old);
    (void)mime;
}

static void
source_cancelled(void *data, struct wl_data_source *src)
{
    (void)data;
    /* Compositor revoked our selection (someone else copied). Free. */
    if (src == g.our_source) {
        wl_data_source_destroy(g.our_source);
        g.our_source = NULL;
        g_clear_pointer(&g.our_text, g_free);
    } else {
        wl_data_source_destroy(src);
    }
}

/* DnD source events — required by listener struct, all no-ops. */
static void source_dnd_drop(void *d, struct wl_data_source *s)        { (void)d; (void)s; }
static void source_dnd_finished(void *d, struct wl_data_source *s)    { (void)d; (void)s; }
static void source_action(void *d, struct wl_data_source *s, uint32_t a){ (void)d; (void)s; (void)a; }

static const struct wl_data_source_listener source_listener = {
    .target              = source_target,
    .send                = source_send,
    .cancelled           = source_cancelled,
    .dnd_drop_performed  = source_dnd_drop,
    .dnd_finished        = source_dnd_finished,
    .action              = source_action,
};

/* ── Public API ─────────────────────────────────────────────────────── */

void
clipboard_init(WaylandState *wl)
{
    memset(&g, 0, sizeof g);
    g.wl = wl;

    if (!wl->data_device_manager || !wl->seat)
        return;

    wl->data_device = wl_data_device_manager_get_data_device(
        wl->data_device_manager, wl->seat);
    if (wl->data_device)
        wl_data_device_add_listener(wl->data_device, &device_listener, NULL);
}

void
clipboard_finish(WaylandState *wl)
{
    if (g.our_source) {
        wl_data_source_destroy(g.our_source);
        g.our_source = NULL;
    }
    g_clear_pointer(&g.our_text, g_free);

    if (g.current_offer) {
        wl_data_offer_destroy(g.current_offer);
        g.current_offer = NULL;
    }
    if (g.pending_offer) {
        wl_data_offer_destroy(g.pending_offer);
        g.pending_offer = NULL;
    }
    g_clear_pointer(&g.current_formats, g_ptr_array_unref);
    g_clear_pointer(&g.pending_formats, g_ptr_array_unref);
    g.wpe_clipboard = NULL;
    if (wl->data_device) {
        wl_data_device_destroy(wl->data_device);
        wl->data_device = NULL;
    }
    if (wl->data_device_manager) {
        wl_data_device_manager_destroy(wl->data_device_manager);
        wl->data_device_manager = NULL;
    }
}

void
clipboard_set_serial(uint32_t serial)
{
    g.serial = serial;
}

void
clipboard_bind_wpe(void *surf_clipboard)
{
    g.wpe_clipboard = (SurfClipboard *)surf_clipboard;
    /* If a selection arrived before binding, push it now. */
    push_formats_to_wpe();
}

bool
clipboard_set_text(const char *utf8)
{
    if (!g.wl || !g.wl->data_device || !g.wl->data_device_manager)
        return false;
    if (!g.serial)
        return false;
    if (!utf8)
        utf8 = "";

    /* Replace previous source. Compositor will deliver `cancelled` for
     * the old source after the new selection is set, but freeing first
     * is safe — `send` checks src == g.our_source. */
    if (g.our_source) {
        wl_data_source_destroy(g.our_source);
        g.our_source = NULL;
    }
    g_clear_pointer(&g.our_text, g_free);
    g.our_text = g_strdup(utf8);

    g.our_source = wl_data_device_manager_create_data_source(g.wl->data_device_manager);
    if (!g.our_source) {
        g_clear_pointer(&g.our_text, g_free);
        return false;
    }
    wl_data_source_add_listener(g.our_source, &source_listener, NULL);
    wl_data_source_offer(g.our_source, MIME_TEXT_UTF8);
    wl_data_source_offer(g.our_source, MIME_UTF8_STRING);
    wl_data_source_offer(g.our_source, MIME_TEXT_PLAIN);

    wl_data_device_set_selection(g.wl->data_device, g.our_source, g.serial);
    wl_display_flush(g.wl->display);
    return true;
}

bool
clipboard_has_text(void)
{
    return g.current_offer && g.current_has_text;
}

/* Pick the best text MIME the offer supports. Caller can pass-through
 * any explicit format request. */
static const char *
pick_text_mime(const char *requested)
{
    if (requested && (strcmp(requested, MIME_TEXT_UTF8) == 0 ||
                      strcmp(requested, MIME_UTF8_STRING) == 0 ||
                      strcmp(requested, MIME_TEXT_PLAIN) == 0))
        return requested;
    return MIME_TEXT_UTF8;
}

char *
clipboard_read_text(size_t *out_size)
{
    if (out_size) *out_size = 0;
    if (!g.wl || !g.current_offer || !g.current_has_text)
        return NULL;

    int pipefd[2];
    if (pipe(pipefd) < 0)
        return NULL;

    const char *mime = pick_text_mime(NULL);
    wl_data_offer_receive(g.current_offer, mime, pipefd[1]);
    close(pipefd[1]);

    /* Flush the receive request, then drain the read fd. The compositor
     * routes data from the source-owner — could be us (loopback) or
     * another client. Roundtrip ensures the request is sent. */
    wl_display_flush(g.wl->display);

    GString *buf = g_string_sized_new(256);
    char chunk[4096];
    for (;;) {
        ssize_t n = read(pipefd[0], chunk, sizeof chunk);
        if (n > 0) {
            g_string_append_len(buf, chunk, n);
            if (buf->len > 8 * 1024 * 1024) break;  /* 8 MiB cap */
            continue;
        }
        if (n == 0) break;
        if (errno == EINTR) continue;
        /* EAGAIN shouldn't happen on blocking pipe, but bail anyway */
        break;
    }
    close(pipefd[0]);

    if (out_size) *out_size = buf->len;
    return g_string_free(buf, FALSE);  /* hand ownership to caller */
}
