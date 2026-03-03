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
static GHashTable *pages_table = NULL;
static GHashTable *hint_elements = NULL;
static guint hint_id_counter = 0;

static void
find_hints(WebKitWebPage *page)
{
	WebKitDOMDocument *doc;
	WebKitDOMNodeList *links, *buttons, *inputs, *clickable;
	GVariantBuilder builder;

	doc = webkit_web_page_get_dom_document(page);
	g_variant_builder_init(&builder, G_VARIANT_TYPE("a(siiii)"));

	/* Clear old hint elements */
	if (hint_elements)
		g_hash_table_remove_all(hint_elements);

	/* Links and areas */
	links = webkit_dom_document_query_selector_all(doc, "a[href], area[href]", NULL);
	if (links) {
		gulong len = webkit_dom_node_list_get_length(links);
		for (gulong i = 0; i < len; i++) {
			WebKitDOMNode *node = webkit_dom_node_list_item(links, i);
			WebKitDOMElement *elem = WEBKIT_DOM_ELEMENT(node);

			WebKitDOMClientRect *rect = webkit_dom_element_get_bounding_client_rect(elem);
			gfloat x = webkit_dom_client_rect_get_left(rect);
			gfloat y = webkit_dom_client_rect_get_top(rect);
			gfloat width = webkit_dom_client_rect_get_width(rect);
			gfloat height = webkit_dom_client_rect_get_height(rect);

			if (width < 3 || height < 3) {
				g_object_unref(rect);
				continue;
			}

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

	/* Buttons - store by ID */
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
				guint id = hint_id_counter++;
				g_hash_table_insert(hint_elements, GUINT_TO_POINTER(id),
				                    g_object_ref(elem));
				
				gchar *marker = g_strdup_printf("[elem:%u]", id);
				g_variant_builder_add(&builder, "(siiii)",
					marker, (gint)x, (gint)y, (gint)width, (gint)height);
				g_free(marker);
			}

			g_object_unref(rect);
		}
		g_object_unref(buttons);
	}

	/* Input fields */
	inputs = webkit_dom_document_query_selector_all(doc,
		"input[type=text], input[type=search], input[type=email], input[type=password], "
		"input[type=tel], input[type=url], input[type=number], "
		"textarea, select, input:not([type])", NULL);
	if (inputs) {
		gulong len = webkit_dom_node_list_get_length(inputs);
		for (gulong i = 0; i < len; i++) {
			WebKitDOMNode *node = webkit_dom_node_list_item(inputs, i);
			WebKitDOMElement *elem = WEBKIT_DOM_ELEMENT(node);

			WebKitDOMClientRect *rect = webkit_dom_element_get_bounding_client_rect(elem);
			gfloat x = webkit_dom_client_rect_get_left(rect);
			gfloat y = webkit_dom_client_rect_get_top(rect);
			gfloat width = webkit_dom_client_rect_get_width(rect);
			gfloat height = webkit_dom_client_rect_get_height(rect);

			if (width >= 3 && height >= 3) {
				guint id = hint_id_counter++;
				g_hash_table_insert(hint_elements, GUINT_TO_POINTER(id),
				                    g_object_ref(elem));
				
				gchar *marker = g_strdup_printf("[input:%u]", id);
				g_variant_builder_add(&builder, "(siiii)",
					marker, (gint)x, (gint)y, (gint)width, (gint)height);
				g_free(marker);
			}

			g_object_unref(rect);
		}
		g_object_unref(inputs);
	}

	/* Generic clickable elements */
	clickable = webkit_dom_document_query_selector_all(doc,
		"[onclick], [role=button], [role=link], [role=tab], [role=menuitem], "
		"[tabindex='0'], [tabindex='1'], [tabindex='2']", NULL);
	if (clickable) {
		gulong len = webkit_dom_node_list_get_length(clickable);
		for (gulong i = 0; i < len; i++) {
			WebKitDOMNode *node = webkit_dom_node_list_item(clickable, i);
			WebKitDOMElement *elem = WEBKIT_DOM_ELEMENT(node);

			WebKitDOMClientRect *rect = webkit_dom_element_get_bounding_client_rect(elem);
			gfloat x = webkit_dom_client_rect_get_left(rect);
			gfloat y = webkit_dom_client_rect_get_top(rect);
			gfloat width = webkit_dom_client_rect_get_width(rect);
			gfloat height = webkit_dom_client_rect_get_height(rect);

			if (width >= 3 && height >= 3) {
				guint id = hint_id_counter++;
				g_hash_table_insert(hint_elements, GUINT_TO_POINTER(id),
				                    g_object_ref(elem));
				
				gchar *marker = g_strdup_printf("[elem:%u]", id);
				g_variant_builder_add(&builder, "(siiii)",
					marker, (gint)x, (gint)y, (gint)width, (gint)height);
				g_free(marker);
			}

			g_object_unref(rect);
		}
		g_object_unref(clickable);
	}

	GVariant *hints = g_variant_builder_end(&builder);
	WebKitUserMessage *msg = webkit_user_message_new("hints-data", hints);
	webkit_web_page_send_message_to_view(page, msg, NULL, NULL, NULL);
}

static void
draw_hints(WebKitWebPage *page, GVariant *hints_data)
{
	WebKitDOMDocument *doc;
	WebKitDOMElement *container;
	GVariantIter iter;
	const gchar *label, *url;
	gint x, y;

	doc = webkit_web_page_get_dom_document(page);

	container = webkit_dom_document_get_element_by_id(doc, "surf-hints-container");
	if (container) {
		webkit_dom_node_remove_child(
			webkit_dom_node_get_parent_node(WEBKIT_DOM_NODE(container)),
			WEBKIT_DOM_NODE(container), NULL);
	}

	container = webkit_dom_document_create_element(doc, "div", NULL);
	webkit_dom_element_set_id(container, "surf-hints-container");
	webkit_dom_element_set_attribute(container, "style",
		"all: initial !important;"
		"position: fixed !important;"
		"top: 0 !important; left: 0 !important;"
		"width: 100vw !important; height: 100vh !important;"
		"z-index: 2147483647 !important;"
		"pointer-events: none !important;"
		"display: block !important;", NULL);

	g_variant_iter_init(&iter, hints_data);
	while (g_variant_iter_loop(&iter, "(ssii)", &label, &url, &x, &y)) {
		WebKitDOMElement *hint;
		WebKitDOMText *text_node;
		gchar *style_str;

		hint = webkit_dom_document_create_element(doc, "div", NULL);
		
		text_node = webkit_dom_document_create_text_node(doc, label);
		webkit_dom_node_append_child(WEBKIT_DOM_NODE(hint),
			WEBKIT_DOM_NODE(text_node), NULL);
		
		style_str = g_strdup_printf(
			"all: initial !important;"
			"position: absolute !important;"
			"left: %dpx !important; top: %dpx !important;"
			"display: inline-block !important;"
			"background: #ffff00 !important;"
			"color: #000000 !important;"
			"font-family: monospace !important;"
			"font-size: 11px !important;"
			"font-weight: bold !important;"
			"line-height: 13px !important;"
			"padding: 2px 4px !important;"
			"border: 1px solid #000000 !important;"
			"border-radius: 2px !important;"
			"z-index: 2147483647 !important;"
			"pointer-events: none !important;"
			"visibility: visible !important;"
			"opacity: 1 !important;"
			"text-align: center !important;"
			"box-shadow: 0 0 3px rgba(0,0,0,0.5) !important;",
			x, y);
		
		webkit_dom_element_set_attribute(hint, "style", style_str, NULL);
		g_free(style_str);

		webkit_dom_node_append_child(WEBKIT_DOM_NODE(container),
			WEBKIT_DOM_NODE(hint), NULL);
	}

	webkit_dom_node_append_child(
		WEBKIT_DOM_NODE(webkit_dom_document_get_document_element(doc)),
		WEBKIT_DOM_NODE(container), NULL);
}

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
    } else if (strcmp(name, "hints-click") == 0) {
        GVariant *data = webkit_user_message_get_parameters(message);
        guint elem_id;
        g_variant_get(data, "(u)", &elem_id);

        /* Clear hints overlay BEFORE clicking */
        WebKitDOMDocument *doc = webkit_web_page_get_dom_document(page);
        WebKitDOMElement *container = webkit_dom_document_get_element_by_id(
            doc, "surf-hints-container");
        if (container) {
            webkit_dom_node_remove_child(
                webkit_dom_node_get_parent_node(WEBKIT_DOM_NODE(container)),
                WEBKIT_DOM_NODE(container), NULL);
        }

        /* Get the stored element and click it directly */
        WebKitDOMElement *elem = g_hash_table_lookup(hint_elements,
                                                      GUINT_TO_POINTER(elem_id));
        if (elem) {
            gchar *tagname = webkit_dom_element_get_tag_name(elem);
            if (tagname && (strcmp(tagname, "INPUT") == 0 ||
                            strcmp(tagname, "TEXTAREA") == 0)) {
                webkit_dom_element_focus(elem);
            }
            g_free(tagname);

            /* Dispatch click event directly on the element */
            WebKitDOMEvent *click_event = webkit_dom_document_create_event(doc, "MouseEvents", NULL);
            webkit_dom_event_init_event(click_event, "click", TRUE, TRUE);
            webkit_dom_event_target_dispatch_event(WEBKIT_DOM_EVENT_TARGET(elem), click_event, NULL);
        }

        return TRUE;
    }

	return FALSE;
}

static void
pageusermessagereply(GObject *o, GAsyncResult *r, gpointer page)
{
	WebKitUserMessage *m;

	m = webkit_web_page_send_message_to_view_finish(page, r, NULL);
	if (!m)
		return;

	/* Socket no longer needed - scrolling uses evaluate_javascript */
	fprintf(stderr, "WEBEXT PID=%d: page registered with surf\n", getpid());
}

static void
page_weak_notify(gpointer data, GObject *dead_page)
{
	guint64 pageid = GPOINTER_TO_UINT(data);
	if (pages_table)
		g_hash_table_remove(pages_table, GUINT_TO_POINTER(pageid));
}

void
pagecreated(WebKitWebExtension *e, WebKitWebPage *p, gpointer unused)
{
	WebKitUserMessage *msg;
	guint64 pageid;

	pageid = webkit_web_page_get_id(p);
	fprintf(stderr, "WEBEXT PID=%d: page-created, pageid=%lu\n", getpid(), pageid);

	g_hash_table_insert(pages_table, GUINT_TO_POINTER(pageid), p);

	g_object_weak_ref(G_OBJECT(p), page_weak_notify, GUINT_TO_POINTER(pageid));

	msg = webkit_user_message_new("page-created", NULL);
	webkit_web_page_send_message_to_view(p, msg, NULL, pageusermessagereply, p);
	g_signal_connect(p, "user-message-received",
	                 G_CALLBACK(hint_message_received), NULL);
}

G_MODULE_EXPORT void
webkit_web_extension_initialize(WebKitWebExtension *e)
{
	fprintf(stderr, "WEBEXT PID=%d: web extension initialized\n", getpid());

	webext = e;
	pages_table = g_hash_table_new(g_direct_hash, g_direct_equal);
	hint_elements = g_hash_table_new_full(g_direct_hash, g_direct_equal,
	                                      NULL, g_object_unref);

	g_signal_connect(G_OBJECT(e), "page-created",
	                 G_CALLBACK(pagecreated), NULL);
}
