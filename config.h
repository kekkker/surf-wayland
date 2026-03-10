/* modifier 0 means no modifier */
static int surfuseragent = 1;
static char *fulluseragent = "";
static char *scriptfile = "~/.surf/script.js";
static char *styledir = "~/.surf/styles/";
static char *certdir = "~/.surf/certificates/";
static char *cachedir = "~/.surf/cache/";
static char *cookiefile = "~/.surf/cookies.sqlite";

/* External file picker for <input type="file">: {} replaced with temp file path */
static const char *filepicker_cmd[] = {
	"foot", "-e", "sh", "-c",
	"NNN_PLUG='p:preview-tui' NNN_PREVIEWIMGPROG='/home/kek/.bin/nnn-img2sixel' nnn -a -p '{}'",
	NULL};


static Parameter defconfig[ParameterLast] = {
	[AccessMicrophone] = {
		{.i = 0},
	},
	[AccessWebcam] = {
		{.i = 0},
	},
	[Certificate] = {
		{.i = 0},
	},
	[CaretBrowsing] = {
		{.i = 0},
	},
	[CookiePolicies] = {
		{.v = "@Aa"},
	},
	[DarkMode] = {
		{.i = 1},
	},
	[DefaultCharset] = {
		{.v = "UTF-8"},
	},
	[DiskCache] = {
		{.i = 1},
	},
	[DNSPrefetch] = {
		{.i = 0},
	},
	[Ephemeral] = {
		{.i = 0},
	},
	[FileURLsCrossAccess] = {
		{.i = 0},
	},
	[FontSize] = {
		{.i = 12},
	},
	[Geolocation] = {
		{.i = 0},
	},
	[HideBackground] = {
		{.i = 0},
	},
	[Inspector] = {
		{.i = 1},
	},
	[JavaScript] = {
		{.i = 1},
	},
	[KioskMode] = {
		{.i = 0},
	},
	[LoadImages] = {
		{.i = 1},
	},
	[MediaManualPlay] = {
		{.i = 1},
	},
	[PDFJSviewer] = {
		{.i = 1},
	},
	[PreferredLanguages] = {
		{.v = (char *[]){NULL}},
	},
	[RunInFullscreen] = {
		{.i = 0},
	},
	[ScrollBars] = {
		{.i = 1},
	},
	[ShowIndicators] = {
		{.i = 1},
	},
	[SiteQuirks] = {
		{.i = 1},
	},
	[SmoothScrolling] = {
		{.i = 0},
	},
	[SpellChecking] = {
		{.i = 0},
	},
	[SpellLanguages] = {
		{.v = ((char *[]){"en_US", NULL})},
	},
	[StrictTLS] = {
		{.i = 1},
	},
	[Style] = {
		{.i = 1},
	},
	[WebGL] = {
		{.i = 0},
	},
	[ZoomLevel] = {
		{.f = 1.0},
	},
};

static UriParameters uriparams[] = {
	{
		"(://|\\.)suckless\\.org(/|$)",
		{
			[JavaScript] = {{.i = 0}, 1},
		},
	},
};

static int winsize[] = {800, 600};

static WebKitFindOptions findopts = WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE |
									WEBKIT_FIND_OPTIONS_WRAP_AROUND;

/* DOWNLOAD(URI, referer) */
#define DOWNLOAD(u, r)                                             \
	{                                                              \
		.v = (const char *[])                                      \
		{                                                          \
			"foot", "-e", "/bin/sh", "-c",                         \
				"curl -g -L -J -O -A \"$1\" -b \"$2\" -c \"$2\""   \
				" -e \"$3\" \"$4\"; read",                         \
				"surf-download", useragent, cookiefile, r, u, NULL \
		}                                                          \
	}

/* PLUMB(URI) */
#define PLUMB(u)                           \
	{                                      \
		.v = (const char *[])              \
		{                                  \
			"/bin/sh", "-c",               \
				"xdg-open \"$0\"", u, NULL \
		}                                  \
	}

/* VIDEOPLAY(URI) */
#define VIDEOPLAY(u)                                 \
	{                                                \
		.v = (const char *[])                        \
		{                                            \
			"/bin/sh", "-c",                         \
				"mpv --really-quiet \"$0\"", u, NULL \
		}                                            \
	}

/* styles */
static SiteSpecific styles[] = {
	{".*", "default.css"},
};

/* certificates */
static SiteSpecific certs[] = {
	{"://suckless\\.org/", "suckless.org.crt"},
};

#define MODKEY GDK_CONTROL_MASK

static Key keys[] = {
	/* modifier              keyval          function    arg */

	/* --- Ctrl+ keybinds --- */
	{MODKEY, GDK_KEY_c, stop, {0}},
	{MODKEY, GDK_KEY_s, dl_clear, {0}},
	{MODKEY | GDK_SHIFT_MASK, GDK_KEY_r, reload, {.i = 1}},
	{MODKEY, GDK_KEY_r, reload, {.i = 0}},
	{MODKEY, GDK_KEY_l, navigate, {.i = +1}},
	{MODKEY, GDK_KEY_h, navigate, {.i = -1}},
	{MODKEY, GDK_KEY_j, tab_move, {.i = +1}},
	{MODKEY, GDK_KEY_k, tab_move, {.i = -1}},
	{MODKEY, GDK_KEY_space, scrollv, {.i = +50}},
	{MODKEY, GDK_KEY_b, scrollv, {.i = -50}},
	{MODKEY, GDK_KEY_d, scrollv, {.i = +50}},
	{MODKEY, GDK_KEY_u, scrollv, {.i = -50}},
	{MODKEY, GDK_KEY_p, screenshot, {0}},
	{MODKEY, GDK_KEY_n, find, {.i = +1}},
	{MODKEY | GDK_SHIFT_MASK, GDK_KEY_n, find, {.i = -1}},
	{MODKEY | GDK_SHIFT_MASK, GDK_KEY_p, print, {0}},
	{MODKEY, GDK_KEY_t, showcert, {0}},
	{MODKEY | GDK_SHIFT_MASK, GDK_KEY_a, togglecookiepolicy, {0}},
	{MODKEY, GDK_KEY_o, toggleinspector, {0}},
	{MODKEY | GDK_SHIFT_MASK, GDK_KEY_c, toggle, {.i = CaretBrowsing}},
	{MODKEY | GDK_SHIFT_MASK, GDK_KEY_g, toggle, {.i = Geolocation}},
	{MODKEY | GDK_SHIFT_MASK, GDK_KEY_s, toggle, {.i = JavaScript}},
	{MODKEY | GDK_SHIFT_MASK, GDK_KEY_i, toggle, {.i = LoadImages}},
	{MODKEY | GDK_SHIFT_MASK, GDK_KEY_b, toggle, {.i = ScrollBars}},
	{MODKEY | GDK_SHIFT_MASK, GDK_KEY_t, toggle, {.i = StrictTLS}},
	{MODKEY | GDK_SHIFT_MASK, GDK_KEY_m, toggle, {.i = Style}},
	{MODKEY | GDK_SHIFT_MASK, GDK_KEY_d, toggle, {.i = DarkMode}},
	{MODKEY, GDK_KEY_F1, showxid, {0}},

	/* --- Normal-mode bare keys --- */
	{0, GDK_KEY_Escape, stop, {0}},
	{0, GDK_KEY_F11, togglefullscreen, {0}},
	{0, GDK_KEY_i, toggleinsert, {0}},
	{0, GDK_KEY_o, openbar, {.i = 0}},
	{0, GDK_KEY_e, openbar, {.i = 1}},
	{0, GDK_KEY_slash, opensearch, {0}},
	{0, GDK_KEY_j, scrollv, {.i = +10}},
	{0, GDK_KEY_k, scrollv, {.i = -10}},
	{0, GDK_KEY_u, tab_reopen, {0}},
	{0, GDK_KEY_h, navigate, {.i = -1}},
	{0, GDK_KEY_l, navigate, {.i = +1}},
	{0, GDK_KEY_r, reload, {.i = 0}},
	{0, GDK_KEY_g, scrollv, {.i = -1000000}},
	{GDK_SHIFT_MASK, GDK_KEY_g, scrollv, {.i = +1000000}},
	{0, GDK_KEY_minus, zoom, {.i = -1}},
	{GDK_SHIFT_MASK, GDK_KEY_plus, zoom, {.i = +1}},
	{0, GDK_KEY_equal, zoom, {.i = 0}},
	{0, GDK_KEY_v, find_select_enter, {0}},
	{GDK_SHIFT_MASK, GDK_KEY_v, find_select_line, {0}},
	{0, GDK_KEY_f, hints_start, {.i = HintModeLink}},
	{GDK_SHIFT_MASK, GDK_KEY_f, hints_start, {.i = HintModeNewWindow}},
	{0, GDK_KEY_y, hints_start, {.i = HintModeYank}},
	{GDK_SHIFT_MASK, GDK_KEY_y, clipboard, {.i = 0}},
	{0, GDK_KEY_t, tab_new, {.i = 0}},
	{GDK_SHIFT_MASK, GDK_KEY_o, openbar_newtab, {0}},
	{0, GDK_KEY_d, tab_close, {0}},
	{GDK_SHIFT_MASK, GDK_KEY_j, tab_next, {0}},
	{GDK_SHIFT_MASK, GDK_KEY_k, tab_prev, {0}},
	{GDK_SHIFT_MASK, GDK_KEY_p, tab_pin, {0}},
	{0, GDK_KEY_p, spawnuserscript, {.v = "$HOME/.surf/userscripts/surf-pass"}},
};

/* button definitions */
static Button buttons[] = {
	/* target       event mask      button  function        argument        stop event */
	{OnLink, 0, 2, clicknewtab, {0}, 1},	  /* middle click: background tab */
	{OnLink, MODKEY, 1, clicknewtab, {0}, 1}, /* ctrl+click: background tab */
	{OnAny, 0, 8, clicknavigate, {.i = -1}, 1},
	{OnAny, 0, 9, clicknavigate, {.i = +1}, 1},
	{OnMedia, MODKEY, 1, clickexternplayer, {0}, 1},
};

/* Status bar colors */
static const char *stat_bg_normal = "#000000";
static const char *stat_fg_normal = "#ffffff";
static const char *stat_font = "13px 'Terminus (TTF)'";

static const char *searchengine = "https://searx.syscat.org/?q=%s";
