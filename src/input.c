#include "input.h"
#include "app.h"
#include "actions.h"
#include "tabs.h"
#include "cmdbar.h"
#include "hints.h"

#include <wpe/webkit.h>
#include <string.h>
#include <stdlib.h>

/* ── hint helpers ────────────────────────────────────────────────────────── */

static void hints_send_update(Tab *t)
{
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a(ssii)"));
    for (int i = 0; i < t->hint_count; i++) {
        HintItem *h = &t->hints[i];
        if (t->hint_len == 0 ||
            strncmp(h->label, t->hint_buf, t->hint_len) == 0)
            g_variant_builder_add(&b, "(ssii)",
                h->label, h->url, h->x, h->y);
    }
    webkit_web_view_send_message_to_page(t->wv,
        webkit_user_message_new("hints-update", g_variant_builder_end(&b)),
        NULL, NULL, NULL);
}

static void hints_free(Tab *t)
{
    for (int i = 0; i < t->hint_count; i++) g_free(t->hints[i].url);
    g_free(t->hints);
    t->hints = NULL;
    t->hint_count = 0;
    t->hint_len = 0;
    t->hint_buf[0] = '\0';
}

static void hints_execute(Tab *t, HintItem *h)
{
    webkit_web_view_send_message_to_page(t->wv,
        webkit_user_message_new("hints-clear", NULL), NULL, NULL, NULL);

    if (strncmp(h->url, "[elem:", 6) == 0 || strncmp(h->url, "[input:", 7) == 0) {
        const char *p = strchr(h->url, ':');
        unsigned int eid = p ? (unsigned int)atoi(p + 1) : 0;
        webkit_web_view_send_message_to_page(t->wv,
            webkit_user_message_new("hints-click", g_variant_new("(u)", eid)),
            NULL, NULL, NULL);
    } else {
        webkit_web_view_load_uri(t->wv, h->url);
    }

    hints_free(t);
    t->mode = MODE_NORMAL;
    wpe_view_focus_in(t->view);
}

static void hints_cancel(Tab *t)
{
    webkit_web_view_send_message_to_page(t->wv,
        webkit_user_message_new("hints-clear", NULL), NULL, NULL, NULL);
    hints_free(t);
    t->mode = MODE_NORMAL;
    wpe_view_focus_in(t->view);
}

static gboolean hint_key(Tab *t, guint keyval)
{
    if (keyval == WPE_KEY_Escape) {
        hints_cancel(t);
        app_repaint_chrome();
        return TRUE;
    }
    if (keyval == WPE_KEY_BackSpace) {
        if (t->hint_len > 0) {
            t->hint_buf[--t->hint_len] = '\0';
            hints_send_update(t);
        }
        return TRUE;
    }
    if (keyval < WPE_KEY_a || keyval > WPE_KEY_z)
        return TRUE;

    if (t->hint_len < (int)sizeof(t->hint_buf) - 1) {
        t->hint_buf[t->hint_len++] = (char)keyval;
        t->hint_buf[t->hint_len]   = '\0';
    }

    HintItem *exact = NULL;
    int nmatches = 0;
    for (int i = 0; i < t->hint_count; i++) {
        if (strncmp(t->hints[i].label, t->hint_buf, t->hint_len) == 0) {
            nmatches++;
            if (strcmp(t->hints[i].label, t->hint_buf) == 0)
                exact = &t->hints[i];
        }
    }

    if (exact) {
        hints_execute(t, exact);
        app_repaint_chrome();
        return TRUE;
    }
    if (nmatches == 0) {
        hints_cancel(t);
        app_repaint_chrome();
        return TRUE;
    }
    hints_send_update(t);
    return TRUE;
}

/* keys[] table lives in config.h (copied from config.def.h) */
#ifndef NKEYS
#include "../config.h"
#define NKEYS (sizeof keys / sizeof keys[0])
#endif

/* ── URL / search normalizer ─────────────────────────────────────────────── */

static char *uri_or_search(const char *input)
{
    if (strstr(input, "://") ||
        g_str_has_prefix(input, "about:") ||
        g_str_has_prefix(input, "data:")  ||
        g_str_has_prefix(input, "file:"))
        return g_strdup(input);

    /* Hostname: no spaces, has a dot (or is localhost) */
    if (!strchr(input, ' ') &&
        (strchr(input, '.') || strcmp(input, "localhost") == 0))
        return g_strdup_printf("https://%s", input);

    char *enc = g_uri_escape_string(input, NULL, FALSE);
    char *uri = g_strdup_printf(searchengine, enc);
    g_free(enc);
    return uri;
}

static gboolean on_event(WPEView *view, WPEEvent *event, gpointer data)
{
    (void)view; (void)data;

    if (wpe_event_get_event_type(event) != WPE_EVENT_KEYBOARD_KEY_DOWN)
        return FALSE;

    guint         keyval = wpe_event_keyboard_get_keyval(event);
    WPEModifiers  mods   = wpe_event_get_modifiers(event);

    /* Strip pointer-button modifiers — only care about keyboard modifiers */
    mods &= WPE_MODIFIER_KEYBOARD_CONTROL |
            WPE_MODIFIER_KEYBOARD_SHIFT   |
            WPE_MODIFIER_KEYBOARD_ALT     |
            WPE_MODIFIER_KEYBOARD_META;

    Tab *t = app_active_tab();
    if (!t) return FALSE;

    /* INSERT mode: only Escape exits; everything else goes to the page */
    if (t->mode == MODE_INSERT) {
        if (keyval == WPE_KEY_Escape && !(mods & WPE_MODIFIER_KEYBOARD_CONTROL)) {
            act_normal_mode(NULL);
            return TRUE;
        }
        return FALSE;
    }

    /* COMMAND / SEARCH mode: feed to cmdbar */
    if (t->mode == MODE_COMMAND || t->mode == MODE_SEARCH) {
        gboolean consumed = cmdbar_keypress(&g_app.cmdbar, keyval, mods);
        if (consumed) {
            if (t->mode == MODE_SEARCH && t->finder) {
                const char *text = cmdbar_text(&g_app.cmdbar);
                if (*text)
                    webkit_find_controller_search(t->finder, text,
                        WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE |
                        WEBKIT_FIND_OPTIONS_WRAP_AROUND, G_MAXUINT);
                else
                    webkit_find_controller_search_finish(t->finder);
            }
            app_repaint_chrome();
            return TRUE;
        }
        /* Enter or Escape */
        if (g_app.cmdbar.mode == CMDBAR_INACTIVE) {
            /* Escape */
            if (t->mode == MODE_SEARCH && t->finder)
                webkit_find_controller_search_finish(t->finder);
            t->mode = MODE_NORMAL;
            cmdbar_close(&g_app.cmdbar);
        } else {
            /* Enter — activate */
            CmdBarMode cbmode = g_app.cmdbar.mode;
            char *text = g_strdup(cmdbar_text(&g_app.cmdbar));
            cmdbar_close(&g_app.cmdbar);
            t->mode = MODE_NORMAL;
            if (cbmode == CMDBAR_URL) {
                char *uri = uri_or_search(text);
                webkit_web_view_load_uri(t->wv, uri);
                g_free(uri);
            } else if (cbmode == CMDBAR_URL_NEWTAB) {
                char *uri = uri_or_search(text);
                Arg a = {0};
                act_new_tab(&a);
                Tab *nt = app_active_tab();
                if (nt) webkit_web_view_load_uri(nt->wv, uri);
                g_free(uri);
            }
            /* SEARCH: already live — keep active */
            g_free(text);
        }
        app_repaint_chrome();
        return TRUE;
    }

    /* HINT mode: consume all keys for hint navigation */
    if (t->mode == MODE_HINT)
        return hint_key(t, keyval);

    /* NORMAL mode: dispatch through keys[] table */
    for (guint i = 0; i < NKEYS; i++) {
        if (keys[i].key == keyval && keys[i].mod == mods && keys[i].fn) {
            keys[i].fn(&keys[i].arg);
            return TRUE;
        }
    }

    return FALSE;
}

void input_init(InputState *in, WPEView *view, KeyFn handler, gpointer data)
{
    in->view    = view;
    in->handler = handler;
    in->data    = data;
    g_signal_connect(view, "event", G_CALLBACK(on_event), in);
}
