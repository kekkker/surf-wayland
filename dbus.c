#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "arg.h"
#include "common.h"

#ifdef WAYLAND_SUPPORT
#include <dbus/dbus.h>
#include <glib.h>
#include <webkit2/webkit2.h>

static DBusConnection *dbus_conn = NULL;
static char *service_name = NULL;

/* Function pointers for surf operations */
typedef void (*SurfNavigateFunc)(Client *client, const Arg *arg);
typedef void (*SurfFindFunc)(Client *client, const Arg *arg);

static SurfNavigateFunc surf_navigate_func = NULL;
static SurfFindFunc surf_find_func = NULL;
static void *clients_list = NULL;

/* Forward declarations */
static Client *find_client_by_instance_id(const char *instance_id);
extern Client *clients;

/* Set up function pointers for surf operations */
void
dbus_set_callbacks(void *clients, void (*navigate)(Client *, const Arg *), void (*find)(Client *, const Arg *))
{
	clients_list = clients;
	surf_navigate_func = (SurfNavigateFunc)navigate;
	surf_find_func = (SurfFindFunc)find;
}

void *
dbus_find_client_by_instance_id(const char *instance_id)
{
	return find_client_by_instance_id(instance_id);
}

int
dbus_init(void)
{
#ifdef WAYLAND_SUPPORT
	DBusError err;
	dbus_error_init(&err);

	dbus_conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
	if (dbus_error_is_set(&err)) {
		fprintf(stderr, "DBus Error: %s\n", err.message);
		dbus_error_free(&err);
		return -1;
	}

	/* Request service name */
	if (service_name) {
		free(service_name);
	}
	service_name = g_strdup_printf("org.suckless.surf.instance%d", getpid());

	if (dbus_bus_request_name(dbus_conn, service_name,
	                          DBUS_NAME_FLAG_REPLACE_EXISTING, &err) !=
	    DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		fprintf(stderr, "Failed to request DBus name: %s\n", err.message);
		dbus_error_free(&err);
		return -1;
	}

	return 0;
#else
	return -1;
#endif
}

void
dbus_cleanup(void)
{
	if (dbus_conn) {
		dbus_connection_unref(dbus_conn);
		dbus_conn = NULL;
	}
	if (service_name) {
		free(service_name);
		service_name = NULL;
	}
}

static Client *
find_client_by_instance_id(const char *instance_id)
{
	Client *c;
	for (c = clients; c; c = c->next) {
		if (strcmp(c->instance_id, instance_id) == 0) {
			return c;
		}
	}
	return NULL;
}

static DBusHandlerResult
dbus_message_handler(DBusConnection *conn, DBusMessage *msg, void *user_data)
{
	const char *interface = dbus_message_get_interface(msg);
	const char *member = dbus_message_get_member(msg);
	DBusMessage *reply = NULL;
	DBusError err;
	dbus_error_init(&err);

	if (!interface || strcmp(interface, "org.suckless.surf") != 0) {
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	if (strcmp(member, "GetURI") == 0) {
		const char *instance_id;
		if (!dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &instance_id,
		                          DBUS_TYPE_INVALID)) {
			reply = dbus_message_new_error(msg, err.name, err.message);
			dbus_error_free(&err);
		} else {
			Client *c = find_client_by_instance_id(instance_id);
			const char *uri = "about:blank";

			if (c) {
				WebKitWebView *view = WEBKIT_WEB_VIEW(c->view);
				const char *temp_uri = webkit_web_view_get_uri(view);
				if (temp_uri) {
					uri = temp_uri;
				}
			}

			reply = dbus_message_new_method_return(msg);
			dbus_message_append_args(reply, DBUS_TYPE_STRING, &uri,
			                        DBUS_TYPE_INVALID);
		}
	} else if (strcmp(member, "Go") == 0) {
		const char *instance_id, *uri;
		if (!dbus_message_get_args(msg, &err,
		                          DBUS_TYPE_STRING, &instance_id,
		                          DBUS_TYPE_STRING, &uri,
		                          DBUS_TYPE_INVALID)) {
			reply = dbus_message_new_error(msg, err.name, err.message);
			dbus_error_free(&err);
		} else {
			Client *c = find_client_by_instance_id(instance_id);
			if (c && surf_navigate_func) {
				Arg arg = { .v = (void *)uri };
				surf_navigate_func(c, &arg);
			}
			reply = dbus_message_new_method_return(msg);
		}
	} else if (strcmp(member, "Find") == 0) {
		const char *instance_id, *text;
		if (!dbus_message_get_args(msg, &err,
		                          DBUS_TYPE_STRING, &instance_id,
		                          DBUS_TYPE_STRING, &text,
		                          DBUS_TYPE_INVALID)) {
			reply = dbus_message_new_error(msg, err.name, err.message);
			dbus_error_free(&err);
		} else {
			Client *c = find_client_by_instance_id(instance_id);
			if (c && surf_find_func) {
				Arg arg = { .v = (void *)text };
				surf_find_func(c, &arg);
			}
			reply = dbus_message_new_method_return(msg);
		}
	} else if (strcmp(member, "ListInstances") == 0) {
		reply = dbus_message_new_method_return(msg);

		/* Count instances */
		int count = 0;
		Client *c;
		for (c = clients; c; c = c->next) {
			count++;
		}

		/* Create array of instance IDs */
		const char **instances = g_new0(const char *, count + 1);
		count = 0;
		for (c = clients; c; c = c->next) {
			instances[count++] = c->instance_id;
		}

		dbus_message_append_args(reply, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING,
		                        &instances, count, DBUS_TYPE_INVALID);
		g_free(instances);
	}

	if (reply) {
		dbus_connection_send(conn, reply, NULL);
		dbus_message_unref(reply);
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

int
dbus_setup_filters(void)
{
#ifdef WAYLAND_SUPPORT
	if (!dbus_conn) {
		return -1;
	}

	dbus_connection_add_filter(dbus_conn, dbus_message_handler, NULL, NULL);
	return 0;
#else
	return -1;
#endif
}

void
dbus_emit_uri_changed(const char *instance_id, const char *uri)
{
#ifdef WAYLAND_SUPPORT
	if (!dbus_conn) {
		return;
	}

	DBusMessage *msg = dbus_message_new_signal("/org/suckless/surf",
	                                           "org.suckless.surf",
	                                           "URIChanged");
	if (!msg) {
		return;
	}

	dbus_message_append_args(msg,
	                         DBUS_TYPE_STRING, &instance_id,
	                         DBUS_TYPE_STRING, &uri,
	                         DBUS_TYPE_INVALID);

	dbus_connection_send(dbus_conn, msg, NULL);
	dbus_message_unref(msg);
#endif
}

void
dbus_process_events(void)
{
#ifdef WAYLAND_SUPPORT
	if (dbus_conn) {
		dbus_connection_read_write_dispatch(dbus_conn, 0);
	}
#endif
}

#else
/* Stub functions for non-Wayland builds */
int dbus_init(void) { return 0; }
void dbus_cleanup(void) {}
int dbus_setup_filters(void) { return 0; }
void dbus_emit_uri_changed(const char *instance_id, const char *uri) {}
void dbus_process_events(void) {}
#endif