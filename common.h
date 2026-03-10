#define MSGBUFSZ 8

#include "types.h"
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

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
	struct Client *next;
} Client;
