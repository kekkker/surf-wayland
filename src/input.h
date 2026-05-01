#pragma once

#include <glib.h>
#include <wpe/wpe-platform.h>

typedef gboolean (*KeyFn)(guint keyval, WPEModifiers mods, gpointer data);

typedef struct {
    WPEView  *view;
    KeyFn     handler;   /* legacy; unused when keys[] dispatch active */
    gpointer  data;
} InputState;

void input_init(InputState *in, WPEView *view, KeyFn handler, gpointer data);
void input_connect_view(WPEView *view);
