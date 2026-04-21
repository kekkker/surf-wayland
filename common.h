#define MSGBUFSZ 8

#include "types.h"
#include <gtk/gtk.h>
#include <webkit/webkit.h>

#define CLOSED_TAB_MAX 20

typedef enum {
	ModeNormal,
	ModeInsert,
	ModeCommand,
	ModeSearch,
	ModeHint,
	ModeSelect,
} Mode;

typedef enum {
	HintModeLink,
	HintModeNewWindow,
	HintModeYank,
} HintMode;

typedef struct {
	char *label;
	char *url;
	void *element;
	int x, y;
} Hint;

typedef struct {
	GArray *hints;
	char *input;
	HintMode mode;
	int active;
	guint64 pageid;
} HintState;

/*
 * Per-tab state.  Everything that is logically "about this page" lives here.
 * Signals from background tabs update their own Tab, never the Client.
 */
typedef struct {
	WebKitWebView        *view;

	/* page identity */
	guint64               pageid;

	/* navigation / display */
	char                 *title;     /* current page title (owned) */
	char                 *targeturi; /* link/image/media under pointer (owned) */
	int                   hover_link;/* overtitle flag: showing hovered link */
	int                   progress;  /* 0-100 */

	/* TLS */
	GTlsCertificate      *cert;        /* borrowed ref from webkit_web_view_get_tls_info() */
	GTlsCertificate      *failedcert;  /* owned ref (taken on TLS error) */
	GTlsCertificateFlags  tlserr;
	int                   https;
	int                   insecure;
	int                   errorpage;

	/* interaction state */
	Mode                  mode;
	WebKitHitTestResult  *mousepos;

	/* in-page find */
	WebKitFindController *finder;
	const char           *needle;
	int                   find_match_count;
	int                   find_current_match;

	/* hint state */
	HintState             hintstate;

	/* tab metadata */
	int                   pinned;
} Tab;

typedef struct Client {
	/* --- window-level widgets --- */
	GtkWidget            *win;
	GtkWidget            *vbox;
	GtkWidget            *tabbar;
	GtkWidget            *statusbar;
	GtkWidget            *barlabel;
	GtkWidget            *statentry;
	GtkWidget            *dlbar;

	/* --- shared WebKit objects (window-wide) --- */
	WebKitSettings       *settings; /* first tab's settings; shared by related views */
	WebKitWebContext     *context;
	WebKitWebInspector   *inspector; /* inspector of the active tab */

	/* --- tab array --- */
	Tab                  *tabs;
	int                   tabs_count;
	int                   tabs_active;

	/* --- window-level state --- */
	char                  instance_id[64];
	int                   fullscreen;

	/* --- command/search bar transient state --- */
	int                   newtab_pending;

	/* --- download state --- */
	gchar                *dl_pending_uri;
	gchar                *dl_pending_path;

	/* --- closed-tab stack --- */
	char                 *closed_tab_stack[CLOSED_TAB_MAX];
	int                   closed_tab_top;

	/* --- FIFO for userscript communication --- */
	char                 *surf_fifo;
	GIOChannel           *fifo_chan;

	/* --- history popup state (per-window) --- */
	GtkWidget            *history_list;
	GtkWidget            *history_scroll;
	int                   history_selected;

	/* --- split view state --- */
	GtkWidget            *parent_paned;
	WebKitWebView        *split_view;      /* second pane's view, or NULL */
	HintState             split_hintstate; /* hint state for the split pane */
	gboolean              split_focus_end; /* TRUE = end pane has focus */
	int                   split_tab;       /* index of tab that owns the split, or -1 */

	struct Client        *next;
} Client;

static inline Tab *
ctab(Client *c)
{
	return &c->tabs[c->tabs_active];
}

/* Returns the WebKitWebView that should receive commands (split-aware). */
static inline WebKitWebView *
focused_view(Client *c)
{
	if (c->split_focus_end && c->split_view)
		return c->split_view;
	return ctab(c)->view;
}

/* Returns the HintState for the currently focused pane. */
static inline HintState *
focused_hintstate(Client *c)
{
	if (c->split_focus_end && c->split_view)
		return &c->split_hintstate;
	return &ctab(c)->hintstate;
}
