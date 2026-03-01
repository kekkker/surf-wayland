#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <webkit2/webkit-web-extension.h>
#include <webkitdom/webkitdom.h>
#include <webkitdom/WebKitDOMDOMWindowUnstable.h>

#include "types.h"

#define LENGTH(x)   (sizeof(x) / sizeof(x[0]))
#define MSGBUFSZ 8

static WebKitWebExtension *webext;
static GtkWidget *hint_overlay = NULL;
static int sock;

/*
 * Return:
 * 0 No data processed: need more data
 * > 0 Amount of data processed or discarded
 */
static size_t
evalmsg(char *msg, size_t sz)
{
	char js[48];
	WebKitWebPage *page;
	JSCContext *jsc;
	JSCValue *jsv;

	if (!(page = webkit_web_extension_get_page(webext, msg[0])))
		return sz;

	if (sz < 2)
		return 0;

	jsc = webkit_frame_get_js_context(webkit_web_page_get_main_frame(page));
	jsv = NULL;

	switch (msg[1]) {
	case 'h':
		if (sz < 3) {
			sz = 0;
			break;
		}
		sz = 3;
		snprintf(js, sizeof(js),
		         "window.scrollBy(window.innerWidth/100*%hhd,0);",
		         msg[2]);
		jsv = jsc_context_evaluate(jsc, js, -1);
		break;
	case 'v':
		if (sz < 3) {
			sz = 0;
			break;
		}
		sz = 3;
		snprintf(js, sizeof(js),
		         "window.scrollBy(0,window.innerHeight/100*%hhd);",
		         msg[2]);
		jsv = jsc_context_evaluate(jsc, js, -1);
		break;
	default:
		fprintf(stderr, "%s:%d:evalmsg: unknown cmd(%zu): '%#x'\n",
		        __FILE__, __LINE__, sz, msg[1]);
	}

	g_object_unref(jsc);
	if (jsv)
		g_object_unref(jsv);

	return sz;
}

/* Find all clickable elements using DOM */
static void
find_hints(WebKitWebPage *page)
{
	WebKitDOMDocument *doc;
	WebKitDOMNodeList *links, *buttons;
	GVariantBuilder builder;

	doc = webkit_web_page_get_dom_document(page);
	g_variant_builder_init(&builder, G_VARIANT_TYPE("a(siiii)"));

	/* Get all links */
	links = webkit_dom_document_query_selector_all(doc, "a[href], area[href]", NULL);
	if (links) {
		gulong len = webkit_dom_node_list_get_length(links);
		for (gulong i = 0; i < len; i++) {
			WebKitDOMNode *node = webkit_dom_node_list_item(links, i);
			WebKitDOMElement *elem = WEBKIT_DOM_ELEMENT(node);

			/* REMOVED: WebKitDOMElement *html_elem = WEBKIT_DOM_HTML_ELEMENT(elem); */

			/* Get bounding rect */
			WebKitDOMClientRect *rect = webkit_dom_element_get_bounding_client_rect(elem);
			gfloat x = webkit_dom_client_rect_get_left(rect);
			gfloat y = webkit_dom_client_rect_get_top(rect);
			gfloat width = webkit_dom_client_rect_get_width(rect);
			gfloat height = webkit_dom_client_rect_get_height(rect);

			/* Skip invisible elements */
			if (width < 3 || height < 3)
				continue;

			/* Get URL */
			gchar *url = webkit_dom_html_anchor_element_get_href(
				WEBKIT_DOM_HTML_ANCHOR_ELEMENT(elem));

			if (url && strlen(url) > 0) {
				g_variant_builder_add(&builder, "(siiii)",
					url, (gint)x, (gint)y, (gint)width, (gint)height);
			}

			g_free(url);
			g_object_unref(rect);
		}
		g_object_unref(links);
	}

	/* Get all buttons */
	buttons = webkit_dom_document_query_selector_all(doc,
		"button, input[type=button], input[type=submit]", NULL);
	if (buttons) {
		gulong len = webkit_dom_node_list_get_length(buttons);
		for (gulong i = 0; i < len; i++) {
			WebKitDOMNode *node = webkit_dom_node_list_item(buttons, i);
			WebKitDOMElement *elem = WEBKIT_DOM_ELEMENT(node);

			WebKitDOMClientRect *rect = webkit_dom_element_get_bounding_client_rect(elem);
			gfloat x = webkit_dom_client_rect_get_left(rect);
			gfloat y = webkit_dom_client_rect_get_top(rect);
			gfloat width = webkit_dom_client_rect_get_width(rect);
			gfloat height = webkit_dom_client_rect_get_height(rect);

			if (width >= 3 && height >= 3) {
				g_variant_builder_add(&builder, "(siiii)",
					"[button]", (gint)x, (gint)y, (gint)width, (gint)height);
			}

			g_object_unref(rect);
		}
		g_object_unref(buttons);
	}

	GVariant *hints = g_variant_builder_end(&builder);
	WebKitUserMessage *msg = webkit_user_message_new("hints-data", hints);
	webkit_web_page_send_message_to_view(page, msg, NULL, NULL, NULL);
}

/* Draw hint overlays using DOM */
static void
draw_hints(WebKitWebPage *page, GVariant *hints_data)
{
	WebKitDOMDocument *doc;
	WebKitDOMElement *container;
	GVariantIter iter;
	const gchar *label, *url;
	gint x, y;

	doc = webkit_web_page_get_dom_document(page);

	/* Remove old hints */
	container = webkit_dom_document_get_element_by_id(doc, "surf-hints-container");
	if (container) {
		webkit_dom_node_remove_child(
			webkit_dom_node_get_parent_node(WEBKIT_DOM_NODE(container)),
			WEBKIT_DOM_NODE(container), NULL);
	}

	/* Create new container */
	container = webkit_dom_document_create_element(doc, "div", NULL);
	webkit_dom_element_set_id(container, "surf-hints-container");
	webkit_dom_element_set_attribute(container, "style",
		"position: fixed; top: 0; left: 0; width: 100%; height: 100%; "
		"z-index: 2147483647; pointer-events: none;", NULL);

	webkit_dom_node_append_child(
		WEBKIT_DOM_NODE(webkit_dom_document_get_body(doc)),
		WEBKIT_DOM_NODE(container), NULL);

	/* Add hint elements */
	g_variant_iter_init(&iter, hints_data);
	while (g_variant_iter_loop(&iter, "(ssii)", &label, &url, &x, &y)) {
		WebKitDOMElement *hint = webkit_dom_document_create_element(doc, "span", NULL);

		gchar *style = g_strdup_printf(
			"position: absolute; left: %dpx; top: %dpx; "
			"background: #ffff00; color: #000000; "
			"font: bold 12px monospace; padding: 2px 4px; "
			"border: 1px solid #000; z-index: 2147483647;",
			x, y);
		webkit_dom_element_set_attribute(hint, "style", style, NULL);
		g_free(style);

		webkit_dom_element_set_inner_html(hint, label, NULL);
		webkit_dom_node_append_child(WEBKIT_DOM_NODE(container),
			WEBKIT_DOM_NODE(hint), NULL);
	}
}

/* Handle hint messages from surf */
static gboolean
hint_message_received(WebKitWebPage *page, WebKitUserMessage *message)
{
	const gchar *name = webkit_user_message_get_name(message);

	if (strcmp(name, "hints-find-links") == 0) {
		find_hints(page);
		return TRUE;
	} else if (strcmp(name, "hints-update") == 0) {
		GVariant *data = webkit_user_message_get_parameters(message);
		draw_hints(page, data);
		return TRUE;
	} else if (strcmp(name, "hints-clear") == 0) {
		WebKitDOMDocument *doc = webkit_web_page_get_dom_document(page);
		WebKitDOMElement *container = webkit_dom_document_get_element_by_id(
			doc, "surf-hints-container");
		if (container) {
			webkit_dom_node_remove_child(
				webkit_dom_node_get_parent_node(WEBKIT_DOM_NODE(container)),
				WEBKIT_DOM_NODE(container), NULL);
		}
		return TRUE;
	}

	return FALSE;
}

static gboolean
readsock(GIOChannel *s, GIOCondition c, gpointer unused)
{
	static char msg[MSGBUFSZ];
	static size_t msgoff;
	GError *gerr = NULL;
	size_t sz;
	gsize rsz;

	if (g_io_channel_read_chars(s, msg+msgoff, sizeof(msg)-msgoff, &rsz, &gerr) !=
	    G_IO_STATUS_NORMAL) {
		if (gerr) {
			fprintf(stderr, "webext: error reading socket: %s\n",
			        gerr->message);
			g_error_free(gerr);
		}
		return TRUE;
	}
	if (msgoff >= sizeof(msg)) {
		fprintf(stderr, "%s:%d:%s: msgoff: %zu", __FILE__, __LINE__, __func__, msgoff);
		return FALSE;
	}

	for (rsz += msgoff; rsz; rsz -= sz) {
		sz = evalmsg(msg, rsz);
		if (sz == 0) {
			/* need more data */
			break;
		}
		if (sz != rsz) {
			/* continue processing message */
			memmove(msg, msg+sz, rsz-sz);
		}
	}
	msgoff = rsz;

	return TRUE;
}

static void
pageusermessagereply(GObject *o, GAsyncResult *r, gpointer page)
{
	WebKitUserMessage *m;
	GUnixFDList *gfd;
	GIOChannel *gchansock;
	const char *name;
	int nfd;

	m = webkit_web_page_send_message_to_view_finish(page, r, NULL);
	name = webkit_user_message_get_name(m);
	if (strcmp(name, "surf-pipe") != 0) {
		fprintf(stderr, "webext-surf: Unknown User Reply: %s\n", name);
		return;
	}

	gfd = webkit_user_message_get_fd_list(m);
	if ((nfd = g_unix_fd_list_get_length(gfd)) != 1) {
		fprintf(stderr, "webext-surf: Too many file-descriptors: %d\n", nfd);
		return;
	}

	sock = g_unix_fd_list_get(gfd, 0, NULL);

	gchansock = g_io_channel_unix_new(sock);
	g_io_channel_set_encoding(gchansock, NULL, NULL);
	g_io_channel_set_flags(gchansock, g_io_channel_get_flags(gchansock)
	                       | G_IO_FLAG_NONBLOCK, NULL);
	g_io_channel_set_close_on_unref(gchansock, TRUE);
	g_io_add_watch(gchansock, G_IO_IN, readsock, NULL);
}

void
pagecreated(WebKitWebExtension *e, WebKitWebPage *p, gpointer unused)
{
	WebKitUserMessage *msg;

	msg = webkit_user_message_new("page-created", NULL);
	webkit_web_page_send_message_to_view(p, msg, NULL, pageusermessagereply, p);
	g_signal_connect(p, "user-message-received",
	                 G_CALLBACK(hint_message_received), NULL);
}

G_MODULE_EXPORT void
webkit_web_extension_initialize(WebKitWebExtension *e)
{
	webext = e;

	g_signal_connect(G_OBJECT(e), "page-created",
	                 G_CALLBACK(pagecreated), NULL);
}
