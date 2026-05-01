#include "cmdbar.h"
#include <string.h>
#include <wpe/wpe-platform.h>

void cmdbar_open(CmdBar *cb, CmdBarMode mode, const char *prefill)
{
    cb->mode = mode;
    cb->len  = 0;
    cb->buf[0] = '\0';

    switch (mode) {
    case CMDBAR_URL:        cb->prompt = " Go: ";   break;
    case CMDBAR_URL_NEWTAB: cb->prompt = " New: ";  break;
    case CMDBAR_SEARCH:     cb->prompt = " Find: "; break;
    case CMDBAR_DOWNLOAD:   cb->prompt = " Save to: "; break;
    default:                cb->prompt = " : ";     break;
    }

    if (prefill) {
        int n = (int)strlen(prefill);
        if (n >= CMDBAR_MAXLEN) n = CMDBAR_MAXLEN - 1;
        memcpy(cb->buf, prefill, n);
        cb->buf[n] = '\0';
        cb->len = n;
    }
    cb->cursor = cb->len;
}

void cmdbar_close(CmdBar *cb)
{
    cb->mode   = CMDBAR_INACTIVE;
    cb->len    = 0;
    cb->cursor = 0;
    cb->buf[0] = '\0';
    cb->prompt = NULL;
}

static void backspace_at(CmdBar *cb)
{
    if (cb->cursor <= 0) return;
    int i = cb->cursor - 1;
    while (i > 0 && (cb->buf[i] & 0xc0) == 0x80) i--;
    int n = cb->cursor - i;
    memmove(cb->buf + i, cb->buf + cb->cursor, cb->len - cb->cursor + 1);
    cb->len -= n;
    cb->cursor = i;
}

gboolean cmdbar_keypress(CmdBar *cb, guint keyval, WPEModifiers mods)
{
    gboolean ctrl = !!(mods & WPE_MODIFIER_KEYBOARD_CONTROL);

    if (ctrl) {
        switch (keyval) {
        case WPE_KEY_a:
            cb->cursor = 0;
            return TRUE;
        case WPE_KEY_e:
            cb->cursor = cb->len;
            return TRUE;
        case WPE_KEY_k:
            cb->len = cb->cursor;
            cb->buf[cb->len] = '\0';
            return TRUE;
        case WPE_KEY_u:
            memmove(cb->buf, cb->buf + cb->cursor, cb->len - cb->cursor + 1);
            cb->len -= cb->cursor;
            cb->cursor = 0;
            return TRUE;
        case WPE_KEY_w: {
            int i = cb->cursor;
            while (i > 0 && cb->buf[i-1] == ' ') i--;
            while (i > 0 && cb->buf[i-1] != ' ') i--;
            int n = cb->cursor - i;
            memmove(cb->buf + i, cb->buf + cb->cursor, cb->len - cb->cursor + 1);
            cb->len -= n;
            cb->cursor = i;
            return TRUE;
        }
        case WPE_KEY_h:
            backspace_at(cb);
            return TRUE;
        default:
            return TRUE;
        }
    }

    switch (keyval) {
    case WPE_KEY_Return:
    case WPE_KEY_KP_Enter:
        return FALSE;

    case WPE_KEY_Escape:
        cb->mode = CMDBAR_INACTIVE;
        return FALSE;

    case WPE_KEY_BackSpace:
        backspace_at(cb);
        return TRUE;

    case WPE_KEY_Delete:
        if (cb->cursor < cb->len) {
            unsigned char c = (unsigned char)cb->buf[cb->cursor];
            int n;
            if      (c < 0x80) n = 1;
            else if (c < 0xe0) n = 2;
            else if (c < 0xf0) n = 3;
            else               n = 4;
            if (cb->cursor + n > cb->len) n = cb->len - cb->cursor;
            memmove(cb->buf + cb->cursor, cb->buf + cb->cursor + n,
                cb->len - cb->cursor - n + 1);
            cb->len -= n;
        }
        return TRUE;

    case WPE_KEY_Left:
        if (cb->cursor > 0) {
            int i = cb->cursor - 1;
            while (i > 0 && (cb->buf[i] & 0xc0) == 0x80) i--;
            cb->cursor = i;
        }
        return TRUE;

    case WPE_KEY_Right:
        if (cb->cursor < cb->len) {
            unsigned char c = (unsigned char)cb->buf[cb->cursor];
            int n;
            if      (c < 0x80) n = 1;
            else if (c < 0xe0) n = 2;
            else if (c < 0xf0) n = 3;
            else               n = 4;
            cb->cursor += n;
            if (cb->cursor > cb->len) cb->cursor = cb->len;
        }
        return TRUE;

    case WPE_KEY_Home:
        cb->cursor = 0;
        return TRUE;

    case WPE_KEY_End:
        cb->cursor = cb->len;
        return TRUE;

    default:
        if (keyval >= 0x20 && keyval != 0x7f && keyval < 0x110000) {
            char utf8[7];
            int n = (int)g_unichar_to_utf8((gunichar)keyval, utf8);
            if (n > 0 && cb->len + n < CMDBAR_MAXLEN - 1) {
                memmove(cb->buf + cb->cursor + n, cb->buf + cb->cursor,
                    cb->len - cb->cursor + 1);
                memcpy(cb->buf + cb->cursor, utf8, n);
                cb->len += n;
                cb->cursor += n;
            }
        }
        return TRUE;
    }
}
