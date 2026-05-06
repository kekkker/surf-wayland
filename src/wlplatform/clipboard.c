/* SurfClipboard — WPEClipboard subclass that bridges WebKit's clipboard
 * API to the Wayland regular clipboard (wl_data_device).
 *
 * Two flow directions:
 *
 *  ↑ copy (WebKit → system):
 *      WebKit calls wpe_clipboard_set_content() during a Copy editing
 *      command. The base class routes that into our `changed` vfunc with
 *      is_local=TRUE — we extract the text and publish it via
 *      clipboard_set_text().
 *
 *  ↓ paste (system → WebKit):
 *      WebKit calls wpe_clipboard_read_text() during a Paste command.
 *      The base class invokes our `read` vfunc with the requested MIME.
 *      We pull bytes synchronously from the latest wl_data_offer.
 *
 * We do NOT push system-side selection changes into the WPEClipboard's
 * internal format list — WebKit's Paste path goes through `read`, which
 * is enough.
 */

#include "clipboard.h"
#include "../clipboard.h"

#include <string.h>

struct _SurfClipboard {
    WPEClipboard parent_instance;
};

G_DEFINE_FINAL_TYPE(SurfClipboard, surf_clipboard, WPE_TYPE_CLIPBOARD)

/* ── vfuncs ──────────────────────────────────────────────────────────── */

static GBytes *
surf_clipboard_read(WPEClipboard *clipboard, const char *format)
{
    (void)clipboard;
    if (!format) return NULL;

    /* Only handle textual formats. WebKit primarily asks for utf-8;
     * we also handle the legacy aliases. */
    if (strcmp(format, "text/plain;charset=utf-8") != 0 &&
        strcmp(format, "text/plain")               != 0 &&
        strcmp(format, "UTF8_STRING")              != 0)
        return NULL;

    size_t n = 0;
    char  *txt = clipboard_read_text(&n);
    if (!txt) return NULL;
    /* GBytes takes ownership of txt via g_free destructor. */
    return g_bytes_new_with_free_func(txt, n, g_free, txt);
}

static void
surf_clipboard_changed(WPEClipboard        *clipboard,
                       GPtrArray           *formats,
                       gboolean             is_local,
                       WPEClipboardContent *content)
{
    (void)clipboard; (void)formats;
    /* Only react to local changes — i.e. WebKit just copied something
     * and we need to push it to the system clipboard. External changes
     * (is_local=FALSE) shouldn't normally hit this path; if they do,
     * ignore them to avoid loops. */
    if (!is_local || !content)
        return;

    const char *text = wpe_clipboard_content_get_text(content);
    if (!text)
        return;

    clipboard_set_text(text);
}

/* ── GObject boilerplate ─────────────────────────────────────────────── */

static void
surf_clipboard_class_init(SurfClipboardClass *klass)
{
    WPEClipboardClass *cclass = WPE_CLIPBOARD_CLASS(klass);
    cclass->read    = surf_clipboard_read;
    cclass->changed = surf_clipboard_changed;
}

static void
surf_clipboard_init(SurfClipboard *self)
{
    (void)self;
}

SurfClipboard *
surf_clipboard_new(WPEDisplay *display)
{
    return SURF_CLIPBOARD(g_object_new(SURF_TYPE_CLIPBOARD,
        "display", display, NULL));
}
