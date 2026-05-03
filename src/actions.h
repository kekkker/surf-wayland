#pragma once

#include <wpe/wpe-platform.h>

typedef union {
    int         i;
    float       f;
    const void *v;
} Arg;

typedef struct {
    WPEModifiers  mod;
    guint         key;
    void        (*fn)(const Arg *);
    Arg           arg;
} Key;

/* Navigation */
void act_navigate(const Arg *a);     /* a.i: +1 forward / -1 back */
void act_reload(const Arg *a);       /* a.i: 1 = bypass cache */
void act_stop(const Arg *a);

/* Scroll / zoom */
void act_scrollv(const Arg *a);      /* a.i: viewport-percent */
void act_scrollh(const Arg *a);
void act_zoom(const Arg *a);         /* a.i: +1/-1/0(reset) */

/* Find */
void act_find_next(const Arg *a);    /* a.i: +1 next / -1 prev */
void act_find_select_enter(const Arg *a);
void act_find_select_line(const Arg *a);
void act_find_select_exit(const Arg *a);

/* Window / tab */
void act_fullscreen(const Arg *a);
void act_new_tab(const Arg *a);
void act_close_tab(const Arg *a);
void act_tab_reopen(const Arg *a);
void act_switch_tab(const Arg *a);   /* a.i: delta (+1/-1) */
void act_quit(const Arg *a);

/* Mode */
void act_insert_mode(const Arg *a);
void act_normal_mode(const Arg *a);

/* Command bar */
void act_open_bar(const Arg *a);     /* a.i: 0=open, 1=edit current URL, 2=new-tab */
void act_open_search(const Arg *a);

/* Hints */
void act_hint_start(const Arg *a);   /* a.i: 0=open, 1=new tab, 2=yank URL */

/* Helper for non-action callers (e.g. hints yank) */
void clipboard_set(const char *text);

/* Tab misc */
void act_pin_tab(const Arg *a);

/* Downloads */
void act_dl_clear(const Arg *a);

/* Settings / toggles */
typedef enum {
    SET_JAVASCRIPT = 0,
    SET_IMAGES,
    SET_CARET,
    SET_DARK,
    SET_STYLE,
    SET_SCROLLBARS,
    SET_STRICT_TLS,
    SET_GEOLOCATION,
    SET_LAST,
} SettingId;

extern int g_settings[SET_LAST];

struct Tab;
void settings_init(void);             /* defaults */
void settings_apply(struct Tab *t);   /* apply current settings to one tab */
void settings_apply_all(void);

void act_toggle_setting(const Arg *a);  /* a.i: SettingId */
void act_toggle_cookies(const Arg *a);

/* Clipboard / external */
void act_clipboard(const Arg *a);       /* a.i: 0=yank URL, 1=paste-and-go */
void act_print(const Arg *a);
void act_inspector(const Arg *a);
void act_show_instance_id(const Arg *a);
void act_reload_userscripts(const Arg *a);
void act_show_cert(const Arg *a);
