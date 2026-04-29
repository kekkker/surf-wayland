#include "input.h"
#include "app.h"
#include "actions.h"
#include "tabs.h"

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

    /* SEARCH / COMMAND mode: handled by command bar (Phase 5); pass through */
    if (t->mode == MODE_SEARCH || t->mode == MODE_COMMAND)
        return FALSE;

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
