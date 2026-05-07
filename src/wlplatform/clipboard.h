#pragma once

#include <wpe/wpe-platform.h>

G_BEGIN_DECLS

#define SURF_TYPE_CLIPBOARD (surf_clipboard_get_type())
G_DECLARE_FINAL_TYPE(SurfClipboard, surf_clipboard, SURF, CLIPBOARD, WPEClipboard)

SurfClipboard *surf_clipboard_new(WPEDisplay *display);

/* Called by src/clipboard.c when wl_data_device.selection delivers a new
 * remote offer. `formats` is a NULL-terminated array of MIME strings owned
 * by the caller for the duration of the call (the function copies into a
 * GPtrArray that becomes owned by WPEClipboard's base class). Pass NULL to
 * clear. */
void surf_clipboard_remote_changed(SurfClipboard *self,
    const char * const *formats);

G_END_DECLS
