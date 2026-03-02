#define MSGBUFSZ 8

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include "types.h"

typedef enum {
	ModeNormal,
	ModeInsert,
	ModeCommand,
	ModeSearch,
	ModeHint,
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
	GtkWidget *statusbar;
	GtkWidget *statentry;
	const char *title, *overtitle, *targeturi;
	const char *needle;
	int newtab_pending;
	struct Client *next;
} Client;

void hints_start(Client *c, const Arg *a);
void hints_cleanup(Client *c);
gboolean hints_keypress(Client *c, GdkEventKey *e);
void tab_new(Client *c, const Arg *a);
void tab_close(Client *c, const Arg *a);
void tab_next(Client *c, const Arg *a);
void tab_prev(Client *c, const Arg *a);
void openbar_newtab(Client *c, const Arg *a);
