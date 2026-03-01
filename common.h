#define MSGBUFSZ 8

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include "types.h"

#ifdef X11_SUPPORT
#include <X11/X.h>
#include <X11/Xlib.h>
#endif

typedef enum {
	ModeNormal,
	ModeInsert,
} Mode;

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
#ifdef X11_SUPPORT
	Window xid;
#endif
	guint64 pageid;
	int progress, fullscreen, https, insecure, errorpage;
	Mode mode;
	const char *title, *overtitle, *targeturi;
	const char *needle;
	struct Client *next;
} Client;
