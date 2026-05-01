#include "filepicker.h"
#include "actions.h"
#include "../config.h"

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    WebKitFileChooserRequest *req;
    gboolean allow_multiple;
    char    *tmpfile;
} PickerData;

static void picker_done(GObject *src, GAsyncResult *res, gpointer ud)
{
    PickerData *pd = ud;
    GError *err = NULL;
    g_subprocess_wait_finish(G_SUBPROCESS(src), res, &err);
    if (err) { g_error_free(err); webkit_file_chooser_request_cancel(pd->req); goto cleanup; }

    char *contents = NULL;
    if (!g_file_get_contents(pd->tmpfile, &contents, NULL, NULL) ||
        !contents || *contents == '\0') {
        webkit_file_chooser_request_cancel(pd->req);
        goto cleanup;
    }

    char **lines = g_strsplit(contents, "\n", -1);
    g_free(contents);

    GPtrArray *paths = g_ptr_array_new();
    int taken = 0;
    for (int i = 0; lines[i]; i++) {
        if (lines[i][0] == '\0') continue;
        if (!pd->allow_multiple && taken > 0) break;
        g_ptr_array_add(paths, lines[i]);
        taken++;
    }
    g_ptr_array_add(paths, NULL);

    if (paths->len > 1)
        webkit_file_chooser_request_select_files(pd->req,
            (const char *const *)paths->pdata);
    else
        webkit_file_chooser_request_cancel(pd->req);

    g_strfreev(lines);
    g_ptr_array_free(paths, FALSE);

cleanup:
    g_unlink(pd->tmpfile);
    g_free(pd->tmpfile);
    g_object_unref(pd->req);
    g_free(pd);
}

static gboolean on_run_file_chooser(WebKitWebView *wv,
    WebKitFileChooserRequest *r, gpointer ud)
{
    (void)wv; (void)ud;
    if (!filepicker_cmd[0]) return FALSE;       /* fall back */

    char *tmpfile = g_strdup("/tmp/surf-filepick-XXXXXX");
    int fd = g_mkstemp(tmpfile);
    if (fd < 0) { g_free(tmpfile); return FALSE; }
    close(fd);

    GPtrArray *argv = g_ptr_array_new();
    for (int i = 0; filepicker_cmd[i]; i++) {
        const char *a = filepicker_cmd[i];
        if (strcmp(a, "{}") == 0) {
            g_ptr_array_add(argv, tmpfile);
        } else if (strstr(a, "{}")) {
            char **parts = g_strsplit(a, "{}", -1);
            char  *rep   = g_strjoinv(tmpfile, parts);
            g_strfreev(parts);
            g_ptr_array_add(argv, rep);
        } else {
            g_ptr_array_add(argv, (gpointer)a);
        }
    }
    g_ptr_array_add(argv, NULL);

    GError *err = NULL;
    GSubprocess *proc = g_subprocess_newv(
        (const char *const *)argv->pdata,
        G_SUBPROCESS_FLAGS_NONE, &err);

    /* Free any replaced strings (those not equal to tmpfile or original) */
    for (guint i = 0; i + 1 < argv->len; i++) {
        gpointer p = argv->pdata[i];
        if (p == tmpfile) continue;
        gboolean is_orig = FALSE;
        for (int k = 0; filepicker_cmd[k]; k++)
            if (p == (gpointer)filepicker_cmd[k]) { is_orig = TRUE; break; }
        if (!is_orig) g_free(p);
    }
    g_ptr_array_free(argv, FALSE);

    if (!proc || err) {
        if (err) g_error_free(err);
        g_unlink(tmpfile);
        g_free(tmpfile);
        return FALSE;
    }

    PickerData *pd = g_new0(PickerData, 1);
    pd->req = g_object_ref(r);
    pd->allow_multiple =
        webkit_file_chooser_request_get_select_multiple(r);
    pd->tmpfile = tmpfile;
    g_subprocess_wait_async(proc, NULL, picker_done, pd);
    g_object_unref(proc);
    return TRUE;
}

void filepicker_install(WebKitWebView *wv)
{
    g_signal_connect(wv, "run-file-chooser",
        G_CALLBACK(on_run_file_chooser), NULL);
}
