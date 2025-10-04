#ifndef DBUS_H
#define DBUS_H

#include "common.h"

/* D-Bus IPC functions for Wayland support */
int dbus_init(void);
void dbus_cleanup(void);
int dbus_setup_filters(void);
void dbus_emit_uri_changed(const char *instance_id, const char *uri);
void dbus_process_events(void);

/* Set up function pointers for surf operations */
void dbus_set_callbacks(void *clients, void (*navigate)(Client *, const Arg *), void (*find)(Client *, const Arg *));
void *dbus_find_client_by_instance_id(const char *instance_id);

#endif