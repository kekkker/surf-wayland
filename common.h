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
	GTlsCertificate      *cert;
	GTlsCertificate      *failedcert;
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

	struct Client        *next;
} Client;

static inline Tab *
ctab(Client *c)
{
	return &c->tabs[c->tabs_active];
}
