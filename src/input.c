#include "input.h"

static gboolean on_event(WPEView *view, WPEEvent *event, gpointer data)
{
    InputState *in = data;
    (void)view;

    if (wpe_event_get_event_type(event) != WPE_EVENT_KEYBOARD_KEY_DOWN)
        return FALSE;

    guint       keyval = wpe_event_keyboard_get_keyval(event);
    WPEModifiers mods  = wpe_event_get_modifiers(event);

    return in->handler(keyval, mods, in->data);
}

void input_init(InputState *in, WPEView *view, KeyFn handler, gpointer data)
{
    in->view    = view;
    in->handler = handler;
    in->data    = data;
    g_signal_connect(view, "event", G_CALLBACK(on_event), in);
}
