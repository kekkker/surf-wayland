#pragma once

#include <wpe/wpe-platform.h>

G_BEGIN_DECLS

#define SURF_TYPE_CLIPBOARD (surf_clipboard_get_type())
G_DECLARE_FINAL_TYPE(SurfClipboard, surf_clipboard, SURF, CLIPBOARD, WPEClipboard)

SurfClipboard *surf_clipboard_new(WPEDisplay *display);

G_END_DECLS
