#include "actions.h"
#include "app.h"
#include "tabs.h"
#include "cmdbar.h"
#include "../config.h"

#include <wpe/webkit.h>
#include <wpe/wpe-platform.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void tab_changed_cb(void *d) { (void)d; app_repaint_chrome(); }

static char *expand_home_path(const char *path)
{
    if (!path || !*path)
        return NULL;
    if (path[0] == '~')
        return g_strconcat(g_get_home_dir(), path + 1, NULL);
    return g_strdup(path);
}

static void relayout_active_view(void)
{
    app_relayout_active();
}

/* ── navigation ──────────────────────────────────────────────────────────── */

void act_navigate(const Arg *a)
{
    Tab *t = app_active_tab();
    if (!t) return;
    if (a->i < 0) webkit_web_view_go_back(t->wv);
    else           webkit_web_view_go_forward(t->wv);
}

void act_reload(const Arg *a)
{
    Tab *t = app_active_tab();
    if (!t) return;
    if (a->i)
        webkit_web_view_reload_bypass_cache(t->wv);
    else
        webkit_web_view_reload(t->wv);
}

void act_stop(const Arg *a)
{
    (void)a;
    Tab *t = app_active_tab();
    if (t) webkit_web_view_stop_loading(t->wv);
}

/* ── scroll / zoom ───────────────────────────────────────────────────────── */

void act_scrollv(const Arg *a)
{
    Tab *t = app_active_tab();
    if (!t) return;
    char js[128];
    snprintf(js, sizeof js,
        "document.scrollingElement.scrollTop+=window.innerHeight/100*%d;",
        a->i);
    webkit_web_view_evaluate_javascript(t->wv, js, -1,
        NULL, NULL, NULL, NULL, NULL);
}

void act_scrollh(const Arg *a)
{
    Tab *t = app_active_tab();
    if (!t) return;
    char js[128];
    snprintf(js, sizeof js,
        "window.scrollBy(window.innerWidth/100*%d,0);", a->i);
    webkit_web_view_evaluate_javascript(t->wv, js, -1,
        NULL, NULL, NULL, NULL, NULL);
}

void act_zoom(const Arg *a)
{
    Tab *t = app_active_tab();
    if (!t) return;
    double cur = webkit_web_view_get_zoom_level(t->wv);
    if      (a->i > 0) webkit_web_view_set_zoom_level(t->wv, cur + 0.1);
    else if (a->i < 0) webkit_web_view_set_zoom_level(t->wv, cur - 0.1);
    else               webkit_web_view_set_zoom_level(t->wv, 1.0);
}

/* ── find ────────────────────────────────────────────────────────────────── */

void act_find_next(const Arg *a)
{
    Tab *t = app_active_tab();
    if (!t || !t->finder) return;
    if (a->i > 0)
        webkit_find_controller_search_next(t->finder);
    else
        webkit_find_controller_search_previous(t->finder);
    app_repaint_chrome();
}

static void js_eval_simple(WebKitWebView *wv, const char *js)
{
    webkit_web_view_evaluate_javascript(wv, js, -1,
        NULL, NULL, NULL, NULL, NULL);
}

void find_select_yank_cb(GObject *obj, GAsyncResult *res, gpointer ud)
{
    (void)res; (void)ud;
    GError *err = NULL;
    webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(obj), res, &err);
    if (err) { g_error_free(err); }
    act_find_select_exit(NULL);
}

void act_find_select_enter(const Arg *a)
{
    (void)a;
    Tab *t = app_active_tab();
    if (!t || t->find_match_count <= 0) return;
    t->mode = MODE_SELECT;
    char js[256];
    snprintf(js, sizeof js,
        "if(window._surfFindSelect)_surfFindSelect(%d);",
        t->find_current_match - 1);
    js_eval_simple(t->wv, js);
    app_repaint_chrome();
}

void act_find_select_line(const Arg *a)
{
    (void)a;
    Tab *t = app_active_tab();
    if (!t || t->find_match_count <= 0) return;
    t->mode = MODE_SELECT;
    char js[512];
    snprintf(js, sizeof js,
        "if(window._surfFindSelect){"
        "_surfFindSelect(%d);"
        "var s=window.getSelection();"
        "s.modify('move','backward','lineboundary');"
        "s.modify('extend','forward','lineboundary');}",
        t->find_current_match - 1);
    js_eval_simple(t->wv, js);
    app_repaint_chrome();
}

void act_find_select_exit(const Arg *a)
{
    (void)a;
    Tab *t = app_active_tab();
    if (!t) return;
    js_eval_simple(t->wv,
        "window.getSelection().removeAllRanges();"
        "if(typeof CSS!=='undefined'&&CSS.highlights)"
        "CSS.highlights.delete('surf-find-sel');");
    t->mode = MODE_NORMAL;
    app_repaint_chrome();
}

/* ── window / tabs ───────────────────────────────────────────────────────── */

void act_fullscreen(const Arg *a)
{
    (void)a;
    if (!g_app.toplevel) return;
    if (g_app.fullscreen) {
        wpe_toplevel_unfullscreen(g_app.toplevel);
        g_app.fullscreen = 0;
    } else {
        wpe_toplevel_fullscreen(g_app.toplevel);
        g_app.fullscreen = 1;
    }
}

void act_new_tab(const Arg *a)
{
    (void)a;
    tabarray_new(&g_app.tabs, WPE_DISPLAY(g_app.display), g_app.toplevel,
        tab_changed_cb, g_app.tab_close_fn, NULL);
    app_repaint_chrome();
}

void act_close_tab(const Arg *a)
{
    (void)a;
    if (g_app.tabs.count == 1) {
        g_main_loop_quit(g_app.loop);
        return;
    }
    tabarray_close(&g_app.tabs, g_app.tabs.active, tab_changed_cb, NULL);
    app_repaint_chrome();
}

void act_switch_tab(const Arg *a)
{
    int n = g_app.tabs.count;
    if (n <= 1) return;
    int next = (g_app.tabs.active + a->i + n) % n;
    tabarray_switch(&g_app.tabs, next);
    app_repaint_chrome();
}

void act_quit(const Arg *a)
{
    (void)a;
    g_main_loop_quit(g_app.loop);
}

/* ── mode ────────────────────────────────────────────────────────────────── */

void act_insert_mode(const Arg *a)
{
    (void)a;
    Tab *t = app_active_tab();
    if (t) {
        t->mode = MODE_INSERT;
        app_repaint_chrome();
    }
}

void act_normal_mode(const Arg *a)
{
    (void)a;
    Tab *t = app_active_tab();
    if (!t) return;
    if (t->mode == MODE_INSERT || t->mode == MODE_SEARCH ||
        t->mode == MODE_COMMAND) {
        if (t->mode == MODE_SEARCH && t->finder)
            webkit_find_controller_search_finish(t->finder);
        if (t->mode == MODE_COMMAND || t->mode == MODE_SEARCH)
            app_cmdbar_clear_history();
        if (t->mode == MODE_COMMAND || t->mode == MODE_SEARCH)
            cmdbar_close(&g_app.cmdbar);
        t->mode = MODE_NORMAL;
        wpe_view_focus_in(t->view);
        app_repaint_chrome();
    }
    webkit_web_view_stop_loading(t->wv);
}

void act_open_bar(const Arg *a)
{
    Tab *t = app_active_tab();
    if (!t) return;
    CmdBarMode mode = (a->i == 2) ? CMDBAR_URL_NEWTAB : CMDBAR_URL;
    const char *prefill = (a->i == 1) ? t->uri : NULL;
    cmdbar_open(&g_app.cmdbar, mode, prefill);
    t->mode = MODE_COMMAND;
    app_cmdbar_refresh_history();
    relayout_active_view();
    app_repaint_chrome();
}

void act_open_search(const Arg *a)
{
    (void)a;
    Tab *t = app_active_tab();
    if (!t) return;
    app_cmdbar_clear_history();
    cmdbar_open(&g_app.cmdbar, CMDBAR_SEARCH, NULL);
    t->mode = MODE_SEARCH;
    relayout_active_view();
    app_repaint_chrome();
}

void act_hint_start(const Arg *a)
{
    Tab *t = app_active_tab();
    if (!t || t->mode != MODE_NORMAL) return;
    t->hint_mode = a ? a->i : 0;
    webkit_web_view_send_message_to_page(t->wv,
        webkit_user_message_new("hints-find-links", NULL),
        NULL, NULL, NULL);
}

/* Push a string into the wl-copy selection. Public so hints can yank. */
void clipboard_set(const char *text)
{
    if (!text || !*text) return;
    GError *err = NULL;
    GSubprocess *p = g_subprocess_new(G_SUBPROCESS_FLAGS_STDIN_PIPE,
        &err, "wl-copy", NULL);
    if (err) { g_warning("wl-copy: %s", err->message); g_error_free(err); return; }
    GBytes *in = g_bytes_new(text, strlen(text));
    g_subprocess_communicate_async(p, in, NULL, NULL, NULL);
    g_bytes_unref(in);
    g_object_unref(p);
}

void act_pin_tab(const Arg *a)
{
    (void)a;
    Tab *t = app_active_tab();
    if (!t) return;
    t->pinned = !t->pinned;
    app_repaint_chrome();
}

void act_dl_clear(const Arg *a)
{
    (void)a;
    downloads_clear(&g_app.dls);
    if (g_app.dlbar) {
        chrome_panel_destroy(g_app.dlbar);
        g_app.dlbar = NULL;
    }
    /* Trigger a re-layout so chrome reclaims the space */
    Tab *t = app_active_tab();
    if (t && t->view)
        app_layout_chrome(wpe_view_get_width(t->view),
            wpe_view_get_height(t->view));
    app_repaint_chrome();
}

/* ── settings / toggles ──────────────────────────────────────────────────── */

int g_settings[SET_LAST];

/* Default toggle values — match config.def.h spirit */
void settings_init(void)
{
    g_settings[SET_JAVASCRIPT]   = 1;
    g_settings[SET_IMAGES]       = 1;
    g_settings[SET_CARET]        = 0;
    g_settings[SET_DARK]         = 0;
    g_settings[SET_STYLE]        = 1;
    g_settings[SET_SCROLLBARS]   = 1;
    g_settings[SET_STRICT_TLS]   = 1;
    g_settings[SET_GEOLOCATION]  = 0;
    g_app.cookie_policy = WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY;
}

static char *read_styledir_default(void)
{
    if (!styledir || !*styledir) return NULL;
    char *expanded = NULL;
    if (styledir[0] == '~') {
        const char *home = g_get_home_dir();
        expanded = g_strconcat(home, styledir + 1, "default.css", NULL);
    } else {
        expanded = g_strconcat(styledir, "default.css", NULL);
    }
    char *contents = NULL;
    g_file_get_contents(expanded, &contents, NULL, NULL);
    g_free(expanded);
    return contents;
}

void settings_apply(struct Tab *t)
{
    if (!t || !t->wv) return;

    WebKitSettings *ws = webkit_web_view_get_settings(t->wv);
    webkit_settings_set_enable_javascript(ws, g_settings[SET_JAVASCRIPT]);
    webkit_settings_set_auto_load_images(ws, g_settings[SET_IMAGES]);
    webkit_settings_set_enable_caret_browsing(ws, g_settings[SET_CARET]);

    /* TLS strictness on this view's network session */
    WebKitNetworkSession *ns = webkit_web_view_get_network_session(t->wv);
    if (ns)
        webkit_network_session_set_tls_errors_policy(ns,
            g_settings[SET_STRICT_TLS] ? WEBKIT_TLS_ERRORS_POLICY_FAIL
                                       : WEBKIT_TLS_ERRORS_POLICY_IGNORE);

    /* Cookie accept policy */
    if (ns) {
        WebKitCookieManager *cm = webkit_network_session_get_cookie_manager(ns);
        if (cm) {
            char *path = expand_home_path(cookiefile);
            if (path) {
                char *dir = g_path_get_dirname(path);
                g_mkdir_with_parents(dir, 0700);
                g_free(dir);
                webkit_cookie_manager_set_persistent_storage(cm, path,
                    WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);
                g_free(path);
            }
            webkit_cookie_manager_set_accept_policy(cm,
                (WebKitCookieAcceptPolicy)g_app.cookie_policy);
        }
    }

    /* User stylesheets — replace the lot every apply */
    WebKitUserContentManager *ucm =
        webkit_web_view_get_user_content_manager(t->wv);
    if (!ucm) return;
    webkit_user_content_manager_remove_all_style_sheets(ucm);

    GString *css = g_string_new(NULL);
    if (g_settings[SET_DARK]) {
        g_string_append(css,
            "html{color-scheme:dark !important;background:#111;color:#ddd;}\n");
    }
    if (!g_settings[SET_SCROLLBARS]) {
        g_string_append(css,
            "::-webkit-scrollbar{display:none !important;}\n"
            "html{scrollbar-width:none !important;}\n");
    }
    if (g_settings[SET_STYLE]) {
        char *user = read_styledir_default();
        if (user) {
            g_string_append(css, user);
            g_free(user);
        }
    }
    if (css->len > 0) {
        WebKitUserStyleSheet *ss = webkit_user_style_sheet_new(css->str,
            WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
            WEBKIT_USER_STYLE_LEVEL_USER, NULL, NULL);
        webkit_user_content_manager_add_style_sheet(ucm, ss);
        webkit_user_style_sheet_unref(ss);
    }
    g_string_free(css, TRUE);
}

void settings_apply_all(void)
{
    for (int i = 0; i < g_app.tabs.count; i++)
        settings_apply(&g_app.tabs.items[i]);
}

void act_toggle_setting(const Arg *a)
{
    int id = a->i;
    if (id < 0 || id >= SET_LAST) return;
    g_settings[id] = !g_settings[id];
    settings_apply_all();
    Tab *t = app_active_tab();
    if (t) webkit_web_view_reload(t->wv);
}

void act_toggle_cookies(const Arg *a)
{
    (void)a;
    g_app.cookie_policy = (g_app.cookie_policy + 1) % 3;
    /* Re-apply just cookie policy — settings_apply_all also covers it */
    settings_apply_all();
}

/* ── clipboard (wl-copy / wl-paste) ──────────────────────────────────────── */

static void wl_paste_cb(GObject *src, GAsyncResult *res, gpointer ud)
{
    (void)ud;
    GError *err = NULL;
    GBytes *out = NULL;
    g_subprocess_communicate_finish(G_SUBPROCESS(src), res, &out, NULL, &err);
    if (err) { g_error_free(err); return; }
    if (!out) return;

    gsize n;
    const char *data = g_bytes_get_data(out, &n);
    char *buf = g_strndup(data, n);
    g_strstrip(buf);
    if (*buf) {
        Tab *t = app_active_tab();
        if (t) webkit_web_view_load_uri(t->wv, buf);
    }
    g_free(buf);
    g_bytes_unref(out);
}

void act_clipboard(const Arg *a)
{
    Tab *t = app_active_tab();
    if (!t) return;
    if (a->i == 0) {
        clipboard_set(webkit_web_view_get_uri(t->wv));
    } else {
        GError *err = NULL;
        GSubprocess *p = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE,
            &err, "wl-paste", "-n", NULL);
        if (err) { g_warning("wl-paste: %s", err->message); g_error_free(err); return; }
        g_subprocess_communicate_async(p, NULL, NULL, wl_paste_cb, NULL);
        g_object_unref(p);
    }
}

/* ── print / inspector / instance id ─────────────────────────────────────── */

void act_print(const Arg *a)
{
    (void)a;
    Tab *t = app_active_tab();
    if (!t) return;
    /* WPE has no native print dialog — fall back to JS window.print().
     * Pages typically render a print preview using page CSS. */
    webkit_web_view_evaluate_javascript(t->wv,
        "window.print()", -1, NULL, NULL, NULL, NULL, NULL);
}

void act_inspector(const Arg *a)
{
    (void)a;
    /* WPE has no embedded inspector. To use it, restart with
     * WEBKIT_INSPECTOR_HTTP_SERVER=127.0.0.1:9222 then point a browser
     * at http://127.0.0.1:9222/ */
    if (!g_getenv("WEBKIT_INSPECTOR_HTTP_SERVER")) {
        fprintf(stderr,
            "surf: inspector not enabled. Restart with "
            "WEBKIT_INSPECTOR_HTTP_SERVER=127.0.0.1:9222\n");
    } else {
        fprintf(stderr,
            "surf: open http://%s/ in another browser to inspect\n",
            g_getenv("WEBKIT_INSPECTOR_HTTP_SERVER"));
    }
}

void act_show_instance_id(const Arg *a)
{
    (void)a;
    Tab *t = app_active_tab();
    fprintf(stderr, "surf: pid=%d tab=%d uri=%s\n",
        (int)getpid(),
        g_app.tabs.active,
        t && t->uri ? t->uri : "(none)");
}

/* ── userscripts ─────────────────────────────────────────────────────────── */

static void load_userscripts_into(WebKitUserContentManager *ucm)
{
    if (!ucm) return;
    webkit_user_content_manager_remove_all_scripts(ucm);

    char *dir = NULL;
    if (scriptfile && scriptfile[0] == '~') {
        dir = g_strconcat(g_get_home_dir(), scriptfile + 1, NULL);
    } else {
        dir = g_strdup(scriptfile);
    }
    /* scriptfile is the user's main script; also scan
     * ~/.surf/userscripts/ for additional .js files */
    char *contents = NULL;
    if (g_file_get_contents(dir, &contents, NULL, NULL)) {
        WebKitUserScript *s = webkit_user_script_new(contents,
            WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
            WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END, NULL, NULL);
        webkit_user_content_manager_add_script(ucm, s);
        webkit_user_script_unref(s);
        g_free(contents);
    }
    g_free(dir);

    char *udir = g_strconcat(g_get_home_dir(), "/.surf/userscripts", NULL);
    GDir *d = g_dir_open(udir, 0, NULL);
    if (d) {
        const char *name;
        while ((name = g_dir_read_name(d))) {
            if (!g_str_has_suffix(name, ".js")) continue;
            char *path = g_build_filename(udir, name, NULL);
            char *src = NULL;
            if (g_file_get_contents(path, &src, NULL, NULL)) {
                WebKitUserScript *s = webkit_user_script_new(src,
                    WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
                    WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END, NULL, NULL);
                webkit_user_content_manager_add_script(ucm, s);
                webkit_user_script_unref(s);
                g_free(src);
            }
            g_free(path);
        }
        g_dir_close(d);
    }
    g_free(udir);
}

void act_reload_userscripts(const Arg *a)
{
    (void)a;
    for (int i = 0; i < g_app.tabs.count; i++) {
        Tab *t = &g_app.tabs.items[i];
        WebKitUserContentManager *ucm =
            webkit_web_view_get_user_content_manager(t->wv);
        load_userscripts_into(ucm);
    }
    Tab *t = app_active_tab();
    if (t) webkit_web_view_reload(t->wv);
}

/* ── show cert ───────────────────────────────────────────────────────────── */

void act_show_cert(const Arg *a)
{
    (void)a;
    Tab *t = app_active_tab();
    if (!t || !t->uri) return;
    /* Map URI → host → certdir/<host>.crt and dump to stderr */
    char *host = NULL;
    GUri *u = g_uri_parse(t->uri, G_URI_FLAGS_NONE, NULL);
    if (u) { host = g_strdup(g_uri_get_host(u)); g_uri_unref(u); }
    if (!host) return;

    char *cdir = certdir[0] == '~'
        ? g_strconcat(g_get_home_dir(), certdir + 1, NULL)
        : g_strdup(certdir);
    char *path = g_strconcat(cdir, host, ".crt", NULL);
    char *contents = NULL;
    if (g_file_get_contents(path, &contents, NULL, NULL)) {
        fprintf(stderr, "surf: %s\n%s\n", path, contents);
        g_free(contents);
    } else {
        fprintf(stderr, "surf: no cert for %s at %s\n", host, path);
    }
    g_free(path); g_free(cdir); g_free(host);
}
