#pragma once

typedef struct {
    char *url;
    int   x, y, w, h;
    char  label[8];
} HintItem;

/* Bijective base-26 label: 0→"a", 25→"z", 26→"aa", ... */
static inline void hints_gen_label(int idx, char *out, int out_size)
{
    char tmp[8];
    int len = 0;
    int n = idx + 1;
    while (n > 0 && len < (int)sizeof(tmp) - 1) {
        n--;
        tmp[len++] = 'a' + (n % 26);
        n /= 26;
    }
    if (len == 0) { out[0] = 'a'; out[1] = '\0'; return; }
    int w = len < out_size - 1 ? len : out_size - 1;
    for (int i = 0; i < w; i++)
        out[i] = tmp[w - 1 - i];
    out[w] = '\0';
}
