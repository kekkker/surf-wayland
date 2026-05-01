#include "tabs.h"
#include "app.h"
#include "filepicker.h"
#include "actions.h"
#include "input.h"
#include "../config.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <wpe/wayland/wpe-wayland.h>
#include <wayland-client.h>

/* ── per-tab signal data ─────────────────────────────────────────────────── */

typedef struct {
    TabArray      *ta;
    WebKitWebView *wv;       /* stable GObject ref — used to find the Tab */
    WPEDisplay    *display;
    WPEToplevel   *toplevel;
    TabChangedFn   on_change;
    TabCloseFn     on_close;
    void          *cb_data;
} TabCBData;

/* Find Tab by wv — safe across realloc because wv pointer is stable. */
static Tab *find_tab(TabCBData *d)
{
    for (int i = 0; i < d->ta->count; i++)
        if (d->ta->items[i].wv == d->wv)
            return &d->ta->items[i];
    return NULL;
}

static void on_notify_uri(GObject *obj, GParamSpec *p, gpointer ud)
{
    (void)p;
    TabCBData *d = ud;
    Tab *t = find_tab(d);
    if (!t) return;
    g_free(t->uri);
    t->uri = g_strdup(webkit_web_view_get_uri(WEBKIT_WEB_VIEW(obj)));
    d->on_change(d->cb_data);
}

static WebKitWebView *on_create(WebKitWebView *wv, WebKitNavigationAction *action,
    gpointer ud)
{
    (void)wv; (void)action;
    TabCBData *d = ud;
    Tab *t = tabarray_new(d->ta, d->display, d->toplevel,
        d->on_change, d->on_close, d->cb_data);
    d->on_change(d->cb_data);
    return t->wv;
}

static void on_webview_close(WebKitWebView *wv, gpointer ud)
{
    (void)wv;
    TabCBData *d = ud;
    Tab *t = find_tab(d);
    if (!t || !d->on_close) return;
    d->on_close((int)(t - d->ta->items), d->cb_data);
}

static void on_mouse_target_changed(WebKitWebView *wv, WebKitHitTestResult *hit,
    guint mods, gpointer ud)
{
    (void)wv; (void)mods;
    TabCBData *d = ud;
    Tab *t = find_tab(d);
    if (!t) return;
    g_free(t->hover_uri);
    t->hover_uri = webkit_hit_test_result_context_is_link(hit)
        ? g_strdup(webkit_hit_test_result_get_link_uri(hit)) : NULL;
    d->on_change(d->cb_data);
}

static void on_web_process_terminated(WebKitWebView *wv,
    WebKitWebProcessTerminationReason reason, gpointer ud)
{
    (void)ud;
    fprintf(stderr, "surf: web process terminated (%d), reloading\n", (int)reason);
    webkit_web_view_reload(wv);
}

static gboolean on_user_message(WebKitWebView *wv, WebKitUserMessage *msg, gpointer ud)
{
    (void)wv;
    TabCBData *d = ud;
    Tab *t = find_tab(d);
    const char *name = webkit_user_message_get_name(msg);

    if (strcmp(name, "hints-data") == 0 && t) {
        for (int i = 0; i < t->hint_count; i++) g_free(t->hints[i].url);
        g_free(t->hints);
        t->hints = NULL;
        t->hint_count = 0;
        t->hint_len = 0;
        t->hint_buf[0] = '\0';

        GVariant *params = webkit_user_message_get_parameters(msg);
        if (params) {
            gsize n = g_variant_n_children(params);
            if (n > 0) {
                t->hints = g_new(HintItem, n);
                GVariantIter iter;
                g_variant_iter_init(&iter, params);
                const char *url;
                gint32 x, y, w, h;
                int i = 0;
                while (g_variant_iter_loop(&iter, "(siiii)", &url, &x, &y, &w, &h)) {
                    t->hints[i].url = g_strdup(url);
                    t->hints[i].x = x; t->hints[i].y = y;
                    t->hints[i].w = w; t->hints[i].h = h;
                    hints_gen_label(i, (int)n, hintkeys,
                        t->hints[i].label, sizeof(t->hints[i].label));
                    i++;
                }
                t->hint_count = i;
            }
        }

        t->mode = MODE_HINT;

        /* Send initial overlay (all hints visible) */
        GVariantBuilder ub;
        g_variant_builder_init(&ub, G_VARIANT_TYPE("a(ssii)"));
        for (int i = 0; i < t->hint_count; i++)
            g_variant_builder_add(&ub, "(ssii)",
                t->hints[i].label, t->hints[i].url,
                t->hints[i].x, t->hints[i].y);
        webkit_web_view_send_message_to_page(d->wv,
            webkit_user_message_new("hints-update", g_variant_builder_end(&ub)),
            NULL, NULL, NULL);

        d->on_change(d->cb_data);
        return TRUE;
    }

    if (strcmp(name, "page-created") == 0)
        return TRUE;

    return FALSE;
}

static void on_find_counted(WebKitFindController *f, guint count, gpointer ud)
{
    (void)f;
    TabCBData *d = ud;
    Tab *t = find_tab(d);
    if (t) { t->find_match_count = (int)count; d->on_change(d->cb_data); }
}

static void on_find_found(WebKitFindController *f, guint count, gpointer ud)
{
    (void)f;
    TabCBData *d = ud;
    Tab *t = find_tab(d);
    if (t) { t->find_current_match = (int)count; d->on_change(d->cb_data); }
}

static void on_load_changed(WebKitWebView *wv, WebKitLoadEvent ev, gpointer ud)
{
    TabCBData *d = ud;
    Tab *t = find_tab(d);
    if (!t) return;
    if (ev == WEBKIT_LOAD_STARTED) {
        t->progress = 0;
        t->https    = 0;
        t->insecure = 0;
        g_clear_pointer(&t->title, g_free);
    }
    g_free(t->uri);
    t->uri = g_strdup(webkit_web_view_get_uri(wv));
    if (t->uri && g_str_has_prefix(t->uri, "https://"))
        t->https = 1;
    if (ev == WEBKIT_LOAD_FINISHED)
        history_add_visit(&g_app.history, t->uri, t->title);
    d->on_change(d->cb_data);
}

static void on_notify_progress(GObject *obj, GParamSpec *p, gpointer ud)
{
    (void)p;
    TabCBData *d = ud;
    Tab *t = find_tab(d);
    if (!t) return;
    t->progress = (int)(
        webkit_web_view_get_estimated_load_progress(WEBKIT_WEB_VIEW(obj)) * 100);
    d->on_change(d->cb_data);
}

static void on_notify_title(GObject *obj, GParamSpec *p, gpointer ud)
{
    (void)p;
    TabCBData *d = ud;
    Tab *t = find_tab(d);
    if (!t) return;
    g_free(t->title);
    t->title = g_strdup(webkit_web_view_get_title(WEBKIT_WEB_VIEW(obj)));
    d->on_change(d->cb_data);
}

static void on_insecure_content(WebKitWebView *wv,
    WebKitInsecureContentEvent e, gpointer ud)
{
    (void)wv; (void)e;
    TabCBData *d = ud;
    Tab *t = find_tab(d);
    if (t) { t->insecure = 1; d->on_change(d->cb_data); }
}

/* ── decide-policy ────────────────────────────────────────────────────────── */

static gboolean on_decide_policy(WebKitWebView *wv, WebKitPolicyDecision *dec,
    WebKitPolicyDecisionType dtype, gpointer ud)
{
    (void)wv;

    switch (dtype) {
    case WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION: {
        WebKitNavigationAction *a =
            webkit_navigation_policy_decision_get_navigation_action(
                WEBKIT_NAVIGATION_POLICY_DECISION(dec));
        /* If targeted at a named frame/iframe, ignore — don't navigate main page */
        if (webkit_navigation_action_get_frame_name(a))
            webkit_policy_decision_ignore(dec);
        else
            webkit_policy_decision_use(dec);
        break;
    }
    case WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION: {
        /* Let WebKit call on_create to get a new WebView */
        webkit_policy_decision_use(dec);
        break;
    }
    case WEBKIT_POLICY_DECISION_TYPE_RESPONSE: {
        WebKitResponsePolicyDecision *r =
            WEBKIT_RESPONSE_POLICY_DECISION(dec);
        if (webkit_response_policy_decision_is_mime_type_supported(r))
            webkit_policy_decision_use(dec);
        else
            webkit_policy_decision_download(dec);
        break;
    }
    default:
        webkit_policy_decision_ignore(dec);
        break;
    }
    return TRUE;
}

/* ── permission-request ───────────────────────────────────────────────────── */

static gboolean on_permission_request(WebKitWebView *wv,
    WebKitPermissionRequest *req, gpointer ud)
{
    (void)wv; (void)ud;

    if (WEBKIT_IS_GEOLOCATION_PERMISSION_REQUEST(req)) {
        if (g_settings[SET_GEOLOCATION])
            webkit_permission_request_allow(req);
        else
            webkit_permission_request_deny(req);
        return TRUE;
    }
    /* Deny media, notifications, XR, etc. */
    webkit_permission_request_deny(req);
    return TRUE;
}

/* ── TLS errors ───────────────────────────────────────────────────────────── */

static gboolean on_tls_error(WebKitWebView *wv, gchar *uri,
    GTlsCertificate *cert, GTlsCertificateFlags err, gpointer ud)
{
    TabCBData *d = ud;
    Tab *t = find_tab(d);
    if (!t) return FALSE;

    t->https = 0;

    GString *msg = g_string_new(NULL);
    if (err & G_TLS_CERTIFICATE_UNKNOWN_CA)
        g_string_append(msg, "The signing certificate authority is not known.<br>");
    if (err & G_TLS_CERTIFICATE_BAD_IDENTITY)
        g_string_append(msg, "The certificate does not match the expected identity.<br>");
    if (err & G_TLS_CERTIFICATE_NOT_ACTIVATED)
        g_string_append(msg, "The certificate's activation time is in the future.<br>");
    if (err & G_TLS_CERTIFICATE_EXPIRED)
        g_string_append(msg, "The certificate has expired.<br>");
    if (err & G_TLS_CERTIFICATE_REVOKED)
        g_string_append(msg, "The certificate has been revoked.<br>");
    if (err & G_TLS_CERTIFICATE_INSECURE)
        g_string_append(msg, "The certificate's algorithm is considered insecure.<br>");
    if (err & G_TLS_CERTIFICATE_GENERIC_ERROR)
        g_string_append(msg, "Some error occurred validating the certificate.<br>");

    gchar *pem = NULL;
    g_object_get(cert, "certificate-pem", &pem, NULL);
    char *html = g_strdup_printf(
        "<html><body style='font-family:sans-serif;padding:2em'>"
        "<h2>TLS Error</h2>"
        "<p>Could not validate TLS for <b>%s</b></p>"
        "<p>%s</p>"
        "<p>Inspect certificate with Ctrl+Shift+X.</p>"
        "<pre>%s</pre></body></html>",
        uri, msg->str, pem ? pem : "");
    g_free(pem);
    g_string_free(msg, TRUE);

    webkit_web_view_load_alternate_html(wv, html, uri, NULL);
    g_free(html);
    return TRUE;
}

/* ── public ──────────────────────────────────────────────────────────────── */

void tabarray_init(TabArray *ta)
{
    ta->items  = NULL;
    ta->count  = 0;
    ta->active = -1;
}

Tab *tabarray_new(TabArray *ta, WPEDisplay *display, WPEToplevel *toplevel,
    TabChangedFn on_change, TabCloseFn on_close, void *cb_data)
{
    ta->count++;
    ta->items = realloc(ta->items, ta->count * sizeof(Tab));
    int idx = ta->count - 1;
    Tab *t = &ta->items[idx];
    memset(t, 0, sizeof *t);
    t->progress = 100;
    t->mode     = MODE_NORMAL;

    if (g_app.network_session) {
        t->wv = g_object_new(WEBKIT_TYPE_WEB_VIEW,
            "display", display,
            "network-session", g_app.network_session,
            NULL);
    } else {
        t->wv = g_object_new(WEBKIT_TYPE_WEB_VIEW, "display", display, NULL);
    }
    t->view = webkit_web_view_get_wpe_view(t->wv);

    /* Move view to our shared toplevel (max_views=0, unlimited).
     * The auto-created per-view toplevel (max_views=1) loses its last
     * ref here and is destroyed, closing the ghost window. */
    if (toplevel)
        wpe_view_set_toplevel(t->view, toplevel);

    t->finder = webkit_web_view_get_find_controller(t->wv);

    TabCBData *cbd = g_new(TabCBData, 1);
    cbd->ta        = ta;
    cbd->wv        = t->wv;
    cbd->display   = display;
    cbd->toplevel  = toplevel;
    cbd->on_change = on_change;
    cbd->on_close  = on_close;
    cbd->cb_data   = cb_data;

    g_signal_connect(t->finder, "counted-matches",
        G_CALLBACK(on_find_counted), cbd);
    g_signal_connect(t->finder, "found-text",
        G_CALLBACK(on_find_found), cbd);

    g_signal_connect(t->wv, "notify::uri",
        G_CALLBACK(on_notify_uri), cbd);
    g_signal_connect(t->wv, "load-changed",
        G_CALLBACK(on_load_changed), cbd);
    g_signal_connect(t->wv, "notify::estimated-load-progress",
        G_CALLBACK(on_notify_progress), cbd);
    g_signal_connect(t->wv, "notify::title",
        G_CALLBACK(on_notify_title), cbd);
    g_signal_connect(t->wv, "insecure-content-detected",
        G_CALLBACK(on_insecure_content), cbd);
    g_signal_connect(t->wv, "create",
        G_CALLBACK(on_create), cbd);
    g_signal_connect(t->wv, "close",
        G_CALLBACK(on_webview_close), cbd);
    g_signal_connect(t->wv, "mouse-target-changed",
        G_CALLBACK(on_mouse_target_changed), cbd);
    g_signal_connect(t->wv, "web-process-terminated",
        G_CALLBACK(on_web_process_terminated), cbd);
    g_signal_connect(t->wv, "user-message-received",
        G_CALLBACK(on_user_message), cbd);
    g_signal_connect(t->wv, "decide-policy",
        G_CALLBACK(on_decide_policy), cbd);
    g_signal_connect(t->wv, "permission-request",
        G_CALLBACK(on_permission_request), cbd);
    g_signal_connect(t->wv, "load-failed-with-tls-errors",
        G_CALLBACK(on_tls_error), cbd);

    filepicker_install(t->wv);
    settings_apply(t);
    input_connect_view(t->view);

    /* Unmap previous active */
    if (ta->active >= 0) {
        wpe_view_unmap(ta->items[ta->active].view);
        wpe_view_set_visible(ta->items[ta->active].view, FALSE);
        wpe_view_focus_out(ta->items[ta->active].view);
    }

    ta->active = idx;
    wpe_view_map(t->view);
    wpe_view_set_visible(t->view, TRUE);
    wpe_view_focus_in(t->view);
    app_raise_chrome();

    return t;
}

void tabarray_close(TabArray *ta, int idx,
    TabChangedFn on_change, void *cb_data)
{
    if (ta->count == 0 || idx < 0 || idx >= ta->count) return;

    Tab *t = &ta->items[idx];
    wpe_view_unmap(t->view);
    g_object_unref(t->wv);
    g_free(t->title);
    g_free(t->uri);
    g_free(t->hover_uri);
    for (int j = 0; j < t->hint_count; j++) g_free(t->hints[j].url);
    g_free(t->hints);

    /* Shift array */
    memmove(&ta->items[idx], &ta->items[idx + 1],
        (ta->count - idx - 1) * sizeof(Tab));
    ta->count--;

    if (ta->count == 0) {
        ta->active = -1;
        return;
    }

    int new_active = idx < ta->count ? idx : ta->count - 1;
    ta->active = new_active;
    wpe_view_set_visible(ta->items[new_active].view, TRUE);
    wpe_view_map(ta->items[new_active].view);
    wpe_view_focus_in(ta->items[new_active].view);
    app_relayout_active();
    on_change(cb_data);
}

void tabarray_switch(TabArray *ta, int idx)
{
    if (idx < 0 || idx >= ta->count || idx == ta->active) return;

    wpe_view_unmap(ta->items[ta->active].view);
    wpe_view_set_visible(ta->items[ta->active].view, FALSE);
    wpe_view_focus_out(ta->items[ta->active].view);
    ta->active = idx;
    wpe_view_map(ta->items[idx].view);
    wpe_view_set_visible(ta->items[idx].view, TRUE);
    wpe_view_focus_in(ta->items[idx].view);
    app_raise_chrome();
    app_relayout_active();

    /* Commit toplevel to finalize view subsurface state. */
    if (g_app.toplevel && WPE_IS_TOPLEVEL_WAYLAND(g_app.toplevel)) {
        struct wl_surface *top = wpe_toplevel_wayland_get_wl_surface(
            WPE_TOPLEVEL_WAYLAND(g_app.toplevel));
        if (top)
            wl_surface_commit(top);
    }
}

void tabarray_free(TabArray *ta)
{
    for (int i = 0; i < ta->count; i++) {
        g_object_unref(ta->items[i].wv);
        g_free(ta->items[i].title);
        g_free(ta->items[i].uri);
        g_free(ta->items[i].hover_uri);
        for (int j = 0; j < ta->items[i].hint_count; j++) g_free(ta->items[i].hints[j].url);
        g_free(ta->items[i].hints);
    }
    free(ta->items);
    ta->items = NULL;
    ta->count = 0;
    ta->active = -1;
}
