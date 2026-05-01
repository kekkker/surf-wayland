#pragma once

#include <glib.h>
#include <time.h>

#define HISTORY_MAX_MATCHES 15

typedef struct {
    long  timestamp;
    char *uri;
    char *title;
} HistoryEntry;

typedef struct {
    const char *uri;
    const char *title;
    long        timestamp;
} HistoryMatch;

typedef struct {
    char   *path;
    GArray *entries;
    time_t  mtime;
} HistoryState;

void history_state_init(HistoryState *hs);
void history_state_free(HistoryState *hs);
void history_add_visit(HistoryState *hs, const char *uri, const char *title);
int history_collect_matches(HistoryState *hs, const char *text,
    HistoryMatch *out, int max_results);
