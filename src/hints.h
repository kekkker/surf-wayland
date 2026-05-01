#pragma once

#include <string.h>

typedef struct {
    char *url;
    int   x, y, w, h;
    char  label[8];
} HintItem;

/* Smallest digit count needed so charset^digits >= total. */
static inline int hints_label_length(int total, const char *charset)
{
    int base = (int)strlen(charset);
    int digits = 1;
    int cap = base;
    while (cap < total) { digits++; cap *= base; }
    return digits;
}

/* Fixed-length label over `charset`. All hints share the same length →
 * no label is a prefix of another. */
static inline void hints_gen_label(int idx, int total, const char *charset,
                                   char *out, int out_size)
{
    int base = (int)strlen(charset);
    int digits = hints_label_length(total, charset);
    if (digits > out_size - 1) digits = out_size - 1;
    for (int i = digits - 1; i >= 0; i--) {
        out[i] = charset[idx % base];
        idx /= base;
    }
    out[digits] = '\0';
}
