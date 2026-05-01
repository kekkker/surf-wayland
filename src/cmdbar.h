#pragma once

#include <glib.h>
#include <wpe/wpe-platform.h>

#define CMDBAR_MAXLEN 4096

typedef enum {
    CMDBAR_INACTIVE,
    CMDBAR_URL,       /* o / e — navigate to URL */
    CMDBAR_URL_NEWTAB,/* O — URL in new tab */
    CMDBAR_SEARCH,    /* / — in-page find */
    CMDBAR_DOWNLOAD,  /* download path prompt */
} CmdBarMode;

typedef struct {
    char         buf[CMDBAR_MAXLEN];
    int          len;        /* bytes used, not including NUL */
    int          cursor;     /* byte offset of insertion point */
    CmdBarMode   mode;
    const char  *prompt;     /* static string, e.g. " Go: " */
} CmdBar;

/* Open the bar. prefill may be NULL. */
void cmdbar_open(CmdBar *cb, CmdBarMode mode, const char *prefill);
void cmdbar_close(CmdBar *cb);

/*
 * Feed a key event.
 * Returns TRUE  = key consumed (redraw needed).
 * Returns FALSE = special key: check cb->mode for INACTIVE (cancel/commit).
 *                 Caller should read buf and act.
 *
 * On Enter:  cb->mode is preserved so caller knows what action to take,
 *            then caller calls cmdbar_close().
 * On Escape: cb->mode set to INACTIVE before returning FALSE.
 */
gboolean cmdbar_keypress(CmdBar *cb, guint keyval, WPEModifiers mods);

/* For live-search: current text (NUL-terminated). */
static inline const char *cmdbar_text(const CmdBar *cb) { return cb->buf; }
