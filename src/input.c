#include "input.h"
#include "app.h"
#include "actions.h"
#include "tabs.h"
#include "cmdbar.h"

#include <wpe/webkit.h>

/* keys[] table lives in config.h (copied from config.def.h) */
#ifndef NKEYS
#include "../config.h"
#define NKEYS (sizeof keys / sizeof keys[0])
#endif

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
                webkit_web_view_load_uri(t->wv, text);
            } else if (cbmode == CMDBAR_URL_NEWTAB) {
                Arg a = {0};
                act_new_tab(&a);
                Tab *nt = app_active_tab();
                if (nt) webkit_web_view_load_uri(nt->wv, text);
            }
            /* SEARCH: already live — keep active */
            g_free(text);
        }
        app_repaint_chrome();
        return TRUE;
    }

    /* NORMAL / HINT mode: dispatch through keys[] table */
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
