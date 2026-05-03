/* surf config — WPE edition
 * WPE_KEY_* names identical to XKB_KEY_* (xkbcommon-keysyms.h).
 * Modifier aliases: MODKEY = WPE_MODIFIER_KEYBOARD_CONTROL
 *                   SHIFT  = WPE_MODIFIER_KEYBOARD_SHIFT
 *                   ALT    = WPE_MODIFIER_KEYBOARD_ALT
 */

#define MODKEY WPE_MODIFIER_KEYBOARD_CONTROL
#define SHIFT WPE_MODIFIER_KEYBOARD_SHIFT
#define ALT WPE_MODIFIER_KEYBOARD_ALT

/* default window size */
static int winsize[] = {1280, 800};

/* paths */
static const char *scriptfile = "~/.surf/script.js";
static const char *styledir = "~/.surf/styles/";
static const char *certdir = "~/.surf/certificates/";
static const char *cachedir = "~/.surf/cache/";
static const char *cookiefile = "~/.surf/cookies.sqlite";

static const char *searchengine = "https://searx.syscat.org/?q=%s";

/* Charset for follow-hint labels. Home-row keys are easy to type. */
static const char *hintkeys = "asdfg";

static const char *filepicker_cmd[] = {
	"foot", "-e", "sh", "-c",
	"nnn -p '{}'",
	NULL};

/* key bindings
 * mod          key               function          arg
 */
static Key keys[] = {
	/* mode */
	{0, WPE_KEY_Escape, act_normal_mode, {0}},
	{0, WPE_KEY_i, act_insert_mode, {0}},

	/* stop / reload */
	{MODKEY, WPE_KEY_c, act_stop, {0}},
	{0, WPE_KEY_r, act_reload, {.i = 0}},
	{MODKEY, WPE_KEY_r, act_reload, {.i = 0}},
	{MODKEY | SHIFT, WPE_KEY_R, act_reload, {.i = 1}},

	/* navigation */
	{0, WPE_KEY_h, act_navigate, {.i = -1}},
	{0, WPE_KEY_l, act_navigate, {.i = +1}},
	{MODKEY, WPE_KEY_h, act_navigate, {.i = -1}},
	{MODKEY, WPE_KEY_l, act_navigate, {.i = +1}},

	/* vertical scroll */
	{0, WPE_KEY_j, act_scrollv, {.i = +10}},
	{0, WPE_KEY_k, act_scrollv, {.i = -10}},
	{MODKEY, WPE_KEY_j, act_scrollv, {.i = +10}}, /* main */
	{MODKEY, WPE_KEY_k, act_scrollv, {.i = -10}}, /* main */
	{MODKEY, WPE_KEY_d, act_scrollv, {.i = +50}},
	{MODKEY, WPE_KEY_space, act_scrollv, {.i = +50}},
	{MODKEY, WPE_KEY_b, act_scrollv, {.i = -50}},
	/* top / bottom */
	{0, WPE_KEY_g, act_scrollv, {.i = -1000000}},
	{SHIFT, WPE_KEY_G, act_scrollv, {.i = +1000000}},

	/* vim-style half-page (Ctrl+u up, Ctrl+d down) */
	{MODKEY, WPE_KEY_u, act_scrollv, {.i = -50}},
	/* horizontal scroll */
	{MODKEY, WPE_KEY_i, act_scrollh, {.i = +10}},
	{ALT, WPE_KEY_h, act_scrollh, {.i = -10}},
	{ALT, WPE_KEY_l, act_scrollh, {.i = +10}},

	/* zoom */
	{0, WPE_KEY_minus, act_zoom, {.i = -1}},
	{SHIFT, WPE_KEY_plus, act_zoom, {.i = +1}},
	{0, WPE_KEY_equal, act_zoom, {.i = 0}},
	{MODKEY, WPE_KEY_minus, act_zoom, {.i = -1}},
	{MODKEY, WPE_KEY_plus, act_zoom, {.i = +1}},
	{MODKEY | SHIFT, WPE_KEY_J, act_zoom, {.i = -1}},
	{MODKEY | SHIFT, WPE_KEY_K, act_zoom, {.i = +1}},
	{MODKEY | SHIFT, WPE_KEY_Q, act_zoom, {.i = 0}},

	/* clipboard / yank
	 *   y      = hint-yank (pick a link, copy its URL)
	 *   Y      = yank current page URL
	 *   p / P  = paste-and-go */
	{MODKEY, WPE_KEY_y, act_clipboard, {.i = 0}},
	{SHIFT, WPE_KEY_Y, act_clipboard, {.i = 0}},
	{0, WPE_KEY_p, act_clipboard, {.i = 1}},
	{MODKEY, WPE_KEY_p, act_clipboard, {.i = 1}},

	/* find */
	{MODKEY, WPE_KEY_n, act_find_next, {.i = +1}},
	{MODKEY | SHIFT, WPE_KEY_N, act_find_next, {.i = -1}},
	{0, WPE_KEY_v, act_find_select_enter, {0}},
	{SHIFT, WPE_KEY_V, act_find_select_line, {0}},

	/* full screen */
	{0, WPE_KEY_F11, act_fullscreen, {0}},
	{MODKEY | SHIFT, WPE_KEY_F, act_fullscreen, {0}},

	/* tabs — switch (no Ctrl+j/k — those are scroll) */
	{SHIFT, WPE_KEY_J, act_switch_tab, {.i = +1}},
	{SHIFT, WPE_KEY_K, act_switch_tab, {.i = -1}},
	{MODKEY, WPE_KEY_Tab, act_switch_tab, {.i = +1}},
	{MODKEY | SHIFT, WPE_KEY_Tab, act_switch_tab, {.i = -1}},

	/* tabs — open / close / pin */
	{0, WPE_KEY_t, act_new_tab, {0}},
	{MODKEY, WPE_KEY_t, act_new_tab, {0}},
	{0, WPE_KEY_d, act_close_tab, {0}},
	{MODKEY, WPE_KEY_w, act_close_tab, {0}},
	{0, WPE_KEY_u, act_tab_reopen, {0}},
	{SHIFT, WPE_KEY_P, act_pin_tab, {0}},

	/* command bar (main: Ctrl+g URL, Ctrl+f/Ctrl+/ find) */
	{0, WPE_KEY_o, act_open_bar, {.i = 0}},
	{0, WPE_KEY_e, act_open_bar, {.i = 1}},
	{SHIFT, WPE_KEY_O, act_open_bar, {.i = 2}},
	{MODKEY, WPE_KEY_g, act_open_bar, {.i = 0}},
	{0, WPE_KEY_slash, act_open_search, {0}},
	{MODKEY, WPE_KEY_f, act_open_search, {0}},
	{MODKEY, WPE_KEY_slash, act_open_search, {0}},

	/* hints
	 *   f / F / y = open / new tab / yank URL */
	{0, WPE_KEY_f, act_hint_start, {.i = 0}},
	{SHIFT, WPE_KEY_F, act_hint_start, {.i = 1}},
	{0, WPE_KEY_y, act_hint_start, {.i = 2}},

	/* settings toggles (main MODKEY|SHIFT scheme) */
	{MODKEY | SHIFT, WPE_KEY_S, act_toggle_setting, {.i = SET_JAVASCRIPT}},
	{MODKEY | SHIFT, WPE_KEY_I, act_toggle_setting, {.i = SET_IMAGES}},
	{MODKEY | SHIFT, WPE_KEY_C, act_toggle_setting, {.i = SET_CARET}},
	{MODKEY | SHIFT, WPE_KEY_D, act_toggle_setting, {.i = SET_DARK}},
	{MODKEY | SHIFT, WPE_KEY_M, act_toggle_setting, {.i = SET_STYLE}},
	{MODKEY | SHIFT, WPE_KEY_B, act_toggle_setting, {.i = SET_SCROLLBARS}},
	{MODKEY | SHIFT, WPE_KEY_T, act_toggle_setting, {.i = SET_STRICT_TLS}},
	{MODKEY | SHIFT, WPE_KEY_G, act_toggle_setting, {.i = SET_GEOLOCATION}},
	{MODKEY | SHIFT, WPE_KEY_A, act_toggle_cookies, {0}},
	{MODKEY | SHIFT, WPE_KEY_U, act_reload_userscripts, {0}},

	/* misc actions */
	{MODKEY | SHIFT, WPE_KEY_P, act_print, {0}},
	{MODKEY | SHIFT, WPE_KEY_O, act_inspector, {0}},
	{MODKEY, WPE_KEY_F1, act_show_instance_id, {0}},
	{0, WPE_KEY_F1, act_show_instance_id, {0}},
	{MODKEY | SHIFT, WPE_KEY_X, act_show_cert, {0}},

	/* downloads (moved off Ctrl+Shift+D — that's DarkMode now) */
	{MODKEY | SHIFT, WPE_KEY_Z, act_dl_clear, {0}},

	/* quit */
	{MODKEY, WPE_KEY_q, act_quit, {0}},
};
