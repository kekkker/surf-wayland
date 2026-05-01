#include "history.h"

#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *history_default_path(void)
{
    return g_build_filename(g_get_home_dir(), ".surf", "history", NULL);
}

static void history_entries_clear(HistoryState *hs)
{
    if (!hs->entries)
        return;
    for (guint i = 0; i < hs->entries->len; i++) {
        HistoryEntry *e = &g_array_index(hs->entries, HistoryEntry, i);
        g_free(e->uri);
        g_free(e->title);
    }
    g_array_set_size(hs->entries, 0);
}

static void history_reload(HistoryState *hs)
{
    GStatBuf st;
    char *contents = NULL;

    if (!hs->path)
        return;

    if (g_stat(hs->path, &st) == 0) {
        if (hs->entries && st.st_mtime == hs->mtime)
            return;
        hs->mtime = st.st_mtime;
    } else {
        hs->mtime = 0;
    }

    if (!hs->entries)
        hs->entries = g_array_new(FALSE, TRUE, sizeof(HistoryEntry));
    history_entries_clear(hs);

    if (!g_file_get_contents(hs->path, &contents, NULL, NULL) || !contents)
        return;

    gchar **lines = g_strsplit(contents, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        char *line = lines[i];
        if (!line[0])
            continue;

        HistoryEntry entry = {0};
        char *space1 = strchr(line, ' ');
        if (!space1)
            continue;

        char *endptr = NULL;
        long ts = strtol(line, &endptr, 10);
        if (endptr == space1) {
            char *url = space1 + 1;
            char *space2 = strchr(url, ' ');
            entry.timestamp = ts;
            if (space2) {
                entry.uri = g_strndup(url, space2 - url);
                entry.title = g_strdup(space2 + 1);
            } else {
                entry.uri = g_strdup(url);
            }
        } else {
            entry.uri = g_strndup(line, space1 - line);
            entry.title = g_strdup(space1 + 1);
        }

        if (!entry.uri || !*entry.uri) {
            g_free(entry.uri);
            g_free(entry.title);
            continue;
        }
        g_array_append_val(hs->entries, entry);
    }

    g_strfreev(lines);
    g_free(contents);
}

static gboolean history_match_text(const HistoryEntry *e, const char *text)
{
    if (!text || !*text)
        return TRUE;

    gchar *uri_lower = g_utf8_strdown(e->uri, -1);
    gchar *text_lower = g_utf8_strdown(text, -1);
    gchar *title_lower = e->title ? g_utf8_strdown(e->title, -1) : NULL;

    gboolean match = strstr(uri_lower, text_lower) != NULL;
    if (!match && title_lower)
        match = strstr(title_lower, text_lower) != NULL;

    if (!match) {
        gchar **words = g_strsplit(text_lower, " ", -1);
        match = TRUE;
        for (int i = 0; words[i]; i++) {
            if (!words[i][0])
                continue;
            gboolean word_match = strstr(uri_lower, words[i]) != NULL;
            if (!word_match && title_lower)
                word_match = strstr(title_lower, words[i]) != NULL;
            if (!word_match) {
                match = FALSE;
                break;
            }
        }
        g_strfreev(words);
    }

    g_free(uri_lower);
    g_free(text_lower);
    g_free(title_lower);
    return match;
}

void history_state_init(HistoryState *hs)
{
    memset(hs, 0, sizeof(*hs));
    hs->path = history_default_path();
}

void history_state_free(HistoryState *hs)
{
    history_entries_clear(hs);
    if (hs->entries)
        g_array_free(hs->entries, TRUE);
    g_free(hs->path);
    memset(hs, 0, sizeof(*hs));
}

void history_add_visit(HistoryState *hs, const char *uri, const char *title)
{
    FILE *f = NULL;
    char *contents = NULL;

    if (!hs->path || !uri || !*uri)
        return;
    if (g_str_has_prefix(uri, "about:") ||
        g_str_has_prefix(uri, "data:") ||
        g_str_has_prefix(uri, "webkit://") ||
        g_str_has_prefix(uri, "file://"))
        return;

    char *dir = g_path_get_dirname(hs->path);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);

    gboolean have_contents =
        g_file_get_contents(hs->path, &contents, NULL, NULL) && contents;
    if (have_contents) {
        gchar **lines = g_strsplit(contents, "\n", -1);
        GString *newcontents = g_string_new(NULL);

        for (int i = g_strv_length(lines) - 1; i >= 0; i--) {
            if (!lines[i] || !*lines[i])
                continue;
            char *space = strchr(lines[i], ' ');
            if (!space)
                break;
            char *saved_url = space + 1;
            char *next_space = strchr(saved_url, ' ');
            gchar *url_only = next_space
                ? g_strndup(saved_url, next_space - saved_url)
                : g_strdup(saved_url);

            if (strcmp(url_only, uri) == 0 && next_space &&
                *(next_space + 1) && (!title || !*title)) {
                g_free(url_only);
                g_string_free(newcontents, TRUE);
                g_strfreev(lines);
                g_free(contents);
                return;
            }
            g_free(url_only);
            break;
        }

        for (int i = 0; lines[i]; i++) {
            if (!lines[i][0])
                continue;
            char *space = strchr(lines[i], ' ');
            if (!space)
                continue;
            char *saved_url = space + 1;
            char *next_space = strchr(saved_url, ' ');
            gchar *url_only = next_space
                ? g_strndup(saved_url, next_space - saved_url)
                : g_strdup(saved_url);
            if (strcmp(url_only, uri) != 0) {
                g_string_append(newcontents, lines[i]);
                g_string_append_c(newcontents, '\n');
            }
            g_free(url_only);
        }

        g_strfreev(lines);
        g_free(contents);

        f = fopen(hs->path, "w");
        if (!f) {
            g_string_free(newcontents, TRUE);
            return;
        }
        fwrite(newcontents->str, 1, newcontents->len, f);
        g_string_free(newcontents, TRUE);
    } else {
        f = fopen(hs->path, "a");
        if (!f)
            return;
    }

    if (title && *title) {
        gchar *safe_title = g_strdup(title);
        for (gchar *p = safe_title; *p; p++) {
            if (*p == '\n' || *p == '\r')
                *p = ' ';
        }
        fprintf(f, "%ld %s %s\n", (long)time(NULL), uri, safe_title);
        g_free(safe_title);
    } else {
        fprintf(f, "%ld %s\n", (long)time(NULL), uri);
    }
    fclose(f);
    hs->mtime = 0;
}

int history_collect_matches(HistoryState *hs, const char *text,
    HistoryMatch *out, int max_results)
{
    int count = 0;
    GHashTable *seen;

    if (!out || max_results <= 0)
        return 0;

    history_reload(hs);
    if (!hs->entries)
        return 0;

    seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (int i = (int)hs->entries->len - 1; i >= 0 && count < max_results; i--) {
        HistoryEntry *e = &g_array_index(hs->entries, HistoryEntry, i);
        if (g_hash_table_contains(seen, e->uri))
            continue;
        if (!history_match_text(e, text))
            continue;
        g_hash_table_add(seen, g_strdup(e->uri));
        out[count].uri = e->uri;
        out[count].title = e->title;
        out[count].timestamp = e->timestamp;
        count++;
    }
    g_hash_table_destroy(seen);
    return count;
}
