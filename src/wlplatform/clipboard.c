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
    /* On local change (WebKit Copy/Cut), push text to wl_data_device.
     * Remote changes route through here too via remote_changed(): we
     * still must chain to parent so priv->formats gets populated —
     * otherwise wpe_clipboard_read_bytes short-circuits to NULL. */
    if (is_local && content) {
        const char *text = wpe_clipboard_content_get_text(content);
        if (text)
            clipboard_set_text(text);
    }

    WPE_CLIPBOARD_CLASS(surf_clipboard_parent_class)->changed(
        clipboard, formats, is_local, content);
}

void
surf_clipboard_remote_changed(SurfClipboard *self,
    const char * const *formats)
{
    if (!self) return;
    GPtrArray *arr = NULL;
    if (formats && formats[0]) {
        arr = g_ptr_array_new();
        for (int i = 0; formats[i]; i++) {
            /* Intern so pointer equality works in g_ptr_array_find,
             * matching wpe_clipboard_read_bytes' lookup path. */
            g_ptr_array_add(arr,
                (gpointer)g_intern_string(formats[i]));
        }
        g_ptr_array_add(arr, NULL);
    }
    WPE_CLIPBOARD_GET_CLASS(self)->changed(WPE_CLIPBOARD(self),
        arr, FALSE, NULL);
    if (arr) g_ptr_array_unref(arr);
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
