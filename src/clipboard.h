#pragma once

#include "wayland.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* Initialize wl_data_device clipboard machinery. Requires wl->seat and
 * wl->data_device_manager already bound. Safe to call without manager —
 * becomes a no-op. */
void clipboard_init(WaylandState *wl);
void clipboard_finish(WaylandState *wl);

/* Bridge to the WPE platform clipboard so WebKit's read-side sees remote
 * offers. Must be called after wpe_display_connect() has created the
 * SurfClipboard. Forward-declared as void* to avoid pulling WPE headers
 * into every clipboard.h consumer. */
void clipboard_bind_wpe(void *surf_clipboard);

/* Track latest input event serial — needed by wl_data_device.set_selection. */
void clipboard_set_serial(uint32_t serial);

/* Publish UTF-8 text as the regular (Ctrl+C) clipboard. Returns false if
 * the manager wasn't available or the seat has no recent serial. */
bool clipboard_set_text(const char *utf8);

/* Synchronously read UTF-8 text from the regular clipboard. Returns
 * newly-allocated NUL-terminated string (free with g_free), or NULL if
 * empty / no offer / not text. *out_size receives byte count (excl. NUL)
 * if non-NULL. */
char *clipboard_read_text(size_t *out_size);

/* Has the regular clipboard a text offer right now? */
bool clipboard_has_text(void);
