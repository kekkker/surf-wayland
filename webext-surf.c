#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <webkit/webkit-web-process-extension.h>
#include <jsc/jsc.h>

#include "types.h"

#define LENGTH(x) (sizeof(x) / sizeof(x[0]))
#define MSGBUFSZ 8

static WebKitWebProcessExtension *webext;
static GHashTable *pages_table = NULL;

/* JavaScript run in the page context to collect clickable hints.
 * Stores clickable/inputtable elements in window.__surfElements[id].
 * Returns a JS array of objects: {url, x, y, w, h} */
static const char find_hints_js[] =
"(function() {"
"  var hints = [];"
"  var vw = window.innerWidth, vh = window.innerHeight;"
"  var sx = window.pageXOffset, sy = window.pageYOffset;"
"  window.__surfElements = {};"
"  var eid = 0;"
"  function ok(r) {"
"    return r.width >= 3 && r.height >= 3 &&"
"           r.left < vw && r.right > 0 && r.top < vh && r.bottom > 0;"
"  }"
"  function addLink(url, el) {"
"    var r = el.getBoundingClientRect();"
"    if (!ok(r)) return;"
"    hints.push({url: url,"
"      x: Math.round(r.left+sx), y: Math.round(r.top+sy),"
"      w: Math.round(r.width), h: Math.round(r.height)});"
"  }"
"  function addClick(prefix, el) {"
"    var r = el.getBoundingClientRect();"
"    if (!ok(r)) return;"
"    var id = eid++;"
"    window.__surfElements[id] = el;"
"    hints.push({url: prefix + id + ']',"
"      x: Math.round(r.left+sx), y: Math.round(r.top+sy),"
"      w: Math.round(r.width), h: Math.round(r.height)});"
"  }"
"  var links = document.querySelectorAll('a[href], area[href]');"
"  for (var i = 0; i < links.length; i++) {"
"    var a = links[i];"
"    var href = a.getAttribute('href') || '';"
"    if (href.indexOf('javascript:') === 0) {"
"      addClick('[elem:', a);"
"    } else if (href && href.indexOf('#') !== 0 && href.indexOf('about:') !== 0) {"
"      addLink(a.href, a);"
"    }"
"  }"
"  var btns = document.querySelectorAll('button, input[type=button], input[type=submit]');"
"  for (var i = 0; i < btns.length; i++) addClick('[elem:', btns[i]);"
"  var inputs = document.querySelectorAll("
"    'input[type=text], input[type=search], input[type=email],"
"     input[type=password], input[type=tel], input[type=url],"
"     input[type=number], textarea, select, input:not([type])');"
"  for (var i = 0; i < inputs.length; i++) addClick('[input:', inputs[i]);"
"  var clickable = document.querySelectorAll("
"    '[onclick], [role=button], [role=link], [role=tab], [role=menuitem],"
"     [tabindex=\"0\"], [tabindex=\"1\"], [tabindex=\"2\"]');"
"  for (var i = 0; i < clickable.length; i++) addClick('[elem:', clickable[i]);"
"  return hints;"
"})()";

static const char clear_hints_js[] =
"(function(){"
"  var c = document.getElementById('surf-hints-container');"
"  if (c) c.parentNode.removeChild(c);"
"})()";

static JSCContext *
web_page_get_main_js_context(WebKitWebPage *page)
{
	WebKitFrame *frame;

	/* WebKitGTK 6 still routes extension-side JS execution through WebKitFrame. */
	G_GNUC_BEGIN_IGNORE_DEPRECATIONS
	frame = webkit_web_page_get_main_frame(page);
	G_GNUC_END_IGNORE_DEPRECATIONS

	if (!frame)
		return NULL;

	return webkit_frame_get_js_context(frame);
}

static void
jsc_eval(WebKitWebPage *page, const char *js)
{
	JSCContext *jsc = web_page_get_main_js_context(page);
	if (!jsc)
		return;
	JSCValue *val = jsc_context_evaluate(jsc, js, -1);
	if (val)
		g_object_unref(val);
	g_object_unref(jsc);
}

static void
find_hints(WebKitWebPage *page)
{
	JSCContext *jsc;
	JSCValue *result, *len_val, *item, *url_v, *x_v, *y_v, *w_v, *h_v;
	GVariantBuilder builder;
	gint32 len, i;

	jsc = web_page_get_main_js_context(page);
	if (!jsc)
		return;

	result = jsc_context_evaluate(jsc, find_hints_js, -1);
	g_object_unref(jsc);

	if (!result || !jsc_value_is_array(result)) {
		if (result)
			g_object_unref(result);
		return;
	}

	g_variant_builder_init(&builder, G_VARIANT_TYPE("a(siiii)"));

	len_val = jsc_value_object_get_property(result, "length");
	len = jsc_value_to_int32(len_val);
	g_object_unref(len_val);

	for (i = 0; i < len; i++) {
		item = jsc_value_object_get_property_at_index(result, i);
		url_v = jsc_value_object_get_property(item, "url");
		x_v   = jsc_value_object_get_property(item, "x");
		y_v   = jsc_value_object_get_property(item, "y");
		w_v   = jsc_value_object_get_property(item, "w");
		h_v   = jsc_value_object_get_property(item, "h");

		char *url = jsc_value_to_string(url_v);
		gint32 x  = jsc_value_to_int32(x_v);
		gint32 y  = jsc_value_to_int32(y_v);
		gint32 w  = jsc_value_to_int32(w_v);
		gint32 h  = jsc_value_to_int32(h_v);

		g_variant_builder_add(&builder, "(siiii)", url, x, y, w, h);

		g_free(url);
		g_object_unref(url_v);
		g_object_unref(x_v);
		g_object_unref(y_v);
		g_object_unref(w_v);
		g_object_unref(h_v);
		g_object_unref(item);
	}
	g_object_unref(result);

	GVariant *hints = g_variant_builder_end(&builder);
	WebKitUserMessage *msg = webkit_user_message_new("hints-data", hints);
	webkit_web_page_send_message_to_view(page, msg, NULL, NULL, NULL);
}

static void
draw_hints(WebKitWebPage *page, GVariant *hints_data)
{
	GVariantIter iter;
	const gchar *label, *url;
	gint x, y;
	GString *json;
	gboolean first = TRUE;
	gchar *js;

	json = g_string_new("[");
	g_variant_iter_init(&iter, hints_data);
	while (g_variant_iter_loop(&iter, "(ssii)", &label, &url, &x, &y)) {
		if (!first)
			g_string_append_c(json, ',');
		first = FALSE;
		gchar *lesc = g_strescape(label, NULL);
		g_string_append_printf(json, "{\"l\":\"%s\",\"x\":%d,\"y\":%d}", lesc, x, y);
		g_free(lesc);
	}
	g_string_append_c(json, ']');

	js = g_strdup_printf(
		"(function(hints){"
		"  var c = document.getElementById('surf-hints-container');"
		"  if (c) c.parentNode.removeChild(c);"
		"  c = document.createElement('div');"
		"  c.id = 'surf-hints-container';"
		"  c.style.cssText = 'all:initial!important;display:block!important;pointer-events:none!important;';"
		"  hints.forEach(function(h){"
		"    var d = document.createElement('div');"
		"    d.textContent = h.l;"
		"    d.style.cssText = 'all:initial!important;position:absolute!important;"
		"left:'+h.x+'px!important;top:'+h.y+'px!important;"
		"display:inline-block!important;background:#ffff00!important;"
		"color:#000000!important;font-family:monospace!important;"
		"font-size:13px!important;font-weight:bold!important;"
		"line-height:13px!important;padding:2px 4px!important;"
		"border:1px solid #000000!important;border-radius:2px!important;"
		"z-index:2147483647!important;pointer-events:none!important;"
		"visibility:visible!important;opacity:1!important;"
		"box-shadow:0 0 3px rgba(0,0,0,0.5)!important;';"
		"    c.appendChild(d);"
		"  });"
		"  document.documentElement.appendChild(c);"
		"})(%s)", json->str);

	g_string_free(json, TRUE);
	jsc_eval(page, js);
	g_free(js);
}

static gboolean
hint_message_received(WebKitWebPage *page, WebKitUserMessage *message)
{
	const gchar *name = webkit_user_message_get_name(message);

	if (strcmp(name, "hints-find-links") == 0) {
		find_hints(page);
		return TRUE;
	}

	if (strcmp(name, "hints-update") == 0) {
		GVariant *data = webkit_user_message_get_parameters(message);
		draw_hints(page, data);
		return TRUE;
	}

	if (strcmp(name, "hints-clear") == 0) {
		jsc_eval(page, clear_hints_js);
		return TRUE;
	}

	if (strcmp(name, "hints-click") == 0) {
		GVariant *data = webkit_user_message_get_parameters(message);
		guint elem_id;
		g_variant_get(data, "(u)", &elem_id);

		gchar *js = g_strdup_printf(
			"(function(id){"
			"  var c = document.getElementById('surf-hints-container');"
			"  if (c) c.parentNode.removeChild(c);"
			"  var el = window.__surfElements && window.__surfElements[id];"
			"  if (el) {"
			"    var tag = (el.tagName||'').toUpperCase();"
			"    if (tag==='INPUT'||tag==='TEXTAREA') el.focus();"
			"    el.dispatchEvent(new MouseEvent('click',{bubbles:true,cancelable:true}));"
			"  }"
			"})(%u)", elem_id);
		jsc_eval(page, js);
		g_free(js);
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
pagecreated(WebKitWebProcessExtension *e, WebKitWebPage *p, gpointer unused)
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
webkit_web_process_extension_initialize(WebKitWebProcessExtension *e)
{
	fprintf(stderr, "WEBEXT PID=%d: web process extension initialized\n", getpid());

	webext = e;
	pages_table = g_hash_table_new(g_direct_hash, g_direct_equal);

	g_signal_connect(G_OBJECT(e), "page-created",
	                 G_CALLBACK(pagecreated), NULL);
}
