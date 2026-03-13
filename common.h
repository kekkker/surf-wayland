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

typedef struct Client {
	GtkWidget *win;
	WebKitWebView *view;
	WebKitSettings *settings;
	WebKitWebContext *context;
	WebKitWebInspector *inspector;
	WebKitFindController *finder;
	WebKitHitTestResult *mousepos;
	GTlsCertificate *cert, *failedcert;
	GTlsCertificateFlags tlserr;
	char instance_id[64];
	guint64 pageid;
	int progress, fullscreen, https, insecure, errorpage;
	int tab_id;
	int tab_pinned;
	Mode mode;
	GtkWidget *vbox;
	GtkWidget *tabbar;
	GtkWidget *statusbar;
	GtkWidget *barlabel;
	GtkWidget *statentry;
	char *title, *overtitle, *targeturi;
	GtkWidget *dlbar;
	gchar *dl_pending_uri;  /* download URI saved while prompting for save path */
	gchar *dl_pending_path; /* confirmed save path, consumed by next decidedestination */
	const char *needle;
	int newtab_pending;
	int find_match_count;
	int find_current_match;
	WebKitWebView **tabs_views;
	gboolean *tab_pins;
	int tabs_count;
	int tabs_active;
	char *closed_tab_stack[CLOSED_TAB_MAX];
	int closed_tab_top;
	char *surf_fifo;
	GIOChannel *fifo_chan;
	struct Client *next;
} Client;
