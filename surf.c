/* See LICENSE file for copyright and license details.
 *
 * To understand surf, start reading main().
 */
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <glib.h>
#include <inttypes.h>
#include <libgen.h>
#include <limits.h>
#include <pwd.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gio/gunixfdlist.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <gcr/gcr.h>
#include <JavaScriptCore/JavaScript.h>
#include <webkit2/webkit2.h>
#include <glib.h>

#ifdef X11_SUPPORT
#include <X11/X.h>
#include <X11/Xatom.h>
#include <gdk/gdkx.h>
#include <gtk/gtkx.h>
#endif

#ifdef WAYLAND_SUPPORT
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <gdk/gdkwayland.h>
#include <dbus/dbus.h>
#endif

#include "arg.h"
#include "common.h"
#include "types.h"
#include "display.h"
#ifdef WAYLAND_SUPPORT
/* D-Bus function declarations */
int dbus_init(void);
void dbus_cleanup(void);
int dbus_setup_filters(void);
void dbus_emit_uri_changed(const char *instance_id, const char *uri);
void dbus_process_events(void);
void dbus_set_callbacks(void *clients, void (*navigate)(Client *, const Arg *), void (*find)(Client *, const Arg *));
void *dbus_find_client_by_instance_id(const char *instance_id);
#endif

#define LENGTH(x)               (sizeof(x) / sizeof(x[0]))
#define CLEANMASK(mask)         (mask & (MODKEY|GDK_SHIFT_MASK))

enum { AtomFind, AtomGo, AtomUri, AtomUTF8, AtomLast };

enum {
	OnDoc   = WEBKIT_HIT_TEST_RESULT_CONTEXT_DOCUMENT,
	OnLink  = WEBKIT_HIT_TEST_RESULT_CONTEXT_LINK,
	OnImg   = WEBKIT_HIT_TEST_RESULT_CONTEXT_IMAGE,
	OnMedia = WEBKIT_HIT_TEST_RESULT_CONTEXT_MEDIA,
	OnEdit  = WEBKIT_HIT_TEST_RESULT_CONTEXT_EDITABLE,
	OnBar   = WEBKIT_HIT_TEST_RESULT_CONTEXT_SCROLLBAR,
	OnSel   = WEBKIT_HIT_TEST_RESULT_CONTEXT_SELECTION,
	OnAny   = OnDoc | OnLink | OnImg | OnMedia | OnEdit | OnBar | OnSel,
};

typedef enum {
	AccessMicrophone,
	AccessWebcam,
	CaretBrowsing,
	Certificate,
	CookiePolicies,
	DarkMode,
	DiskCache,
	DefaultCharset,
	DNSPrefetch,
	Ephemeral,
	FileURLsCrossAccess,
	FontSize,
	Geolocation,
	HideBackground,
	Inspector,
	JavaScript,
	KioskMode,
	LoadImages,
	MediaManualPlay,
	PDFJSviewer,
	PreferredLanguages,
	RunInFullscreen,
	ScrollBars,
	ShowIndicators,
	SiteQuirks,
	SmoothScrolling,
	SpellChecking,
	SpellLanguages,
	StrictTLS,
	Style,
	WebGL,
	ZoomLevel,
	ParameterLast
} ParamName;

typedef struct {
	Arg val;
	int prio;
} Parameter;


typedef struct {
	guint mod;
	guint keyval;
	void (*func)(Client *c, const Arg *a);
	const Arg arg;
} Key;

typedef struct {
	unsigned int target;
	unsigned int mask;
	guint button;
	void (*func)(Client *c, const Arg *a, WebKitHitTestResult *h);
	const Arg arg;
	unsigned int stopevent;
} Button;

typedef struct {
	const char *uri;
	Parameter config[ParameterLast];
	regex_t re;
} UriParameters;

typedef struct {
	char *regex;
	char *file;
	regex_t re;
} SiteSpecific;

/* Surf */
static void die(const char *errstr, ...);
static void usage(void);
static void setup(void);
static void sigchld(int unused);
static void sighup(int unused);
static char *buildfile(const char *path);
static char *buildpath(const char *path);
static char *untildepath(const char *path);
static const char *getuserhomedir(const char *user);
static const char *getcurrentuserhomedir(void);
static Client *newclient(Client *c);
static void loaduri(Client *c, const Arg *a);
static const char *geturi(Client *c);
static void setatom(Client *c, int a, const char *v);
static const char *getatom(Client *c, int a);
static void updatetitle(Client *c);
static void gettogglestats(Client *c);
static void getpagestats(Client *c);
static WebKitCookieAcceptPolicy cookiepolicy_get(void);
static char cookiepolicy_set(const WebKitCookieAcceptPolicy p);
static void seturiparameters(Client *c, const char *uri, ParamName *params);
static void setparameter(Client *c, int refresh, ParamName p, const Arg *a);
static const char *getcert(const char *uri);
static void setcert(Client *c, const char *file);
static const char *getstyle(const char *uri);
static void setstyle(Client *c, const char *file);
static void runscript(Client *c);
static void evalscript(Client *c, const char *jsstr, ...);
static void updatewinid(Client *c);
static void handleplumb(Client *c, const char *uri);
static void newwindow(Client *c, const Arg *a, int noembed);
static void spawn(Client *c, const Arg *a);
static void msgext(Client *c, char type, const Arg *a);
static void destroyclient(Client *c);
static void cleanup(void);

/* GTK/WebKit */
static WebKitWebView *newview(Client *c, WebKitWebView *rv);
static void initwebextensions(WebKitWebContext *wc, Client *c);
static GtkWidget *createview(WebKitWebView *v, WebKitNavigationAction *a,
                             Client *c);
static gboolean buttonreleased(GtkWidget *w, GdkEvent *e, Client *c);
static GdkFilterReturn processx(GdkXEvent *xevent, GdkEvent *event,
                                gpointer d);
static gboolean winevent(GtkWidget *w, GdkEvent *e, Client *c);
static void showview(WebKitWebView *v, Client *c);
static GtkWidget *createwindow(Client *c);
static gboolean loadfailedtls(WebKitWebView *v, gchar *uri,
                              GTlsCertificate *cert,
                              GTlsCertificateFlags err, Client *c);
static void loadchanged(WebKitWebView *v, WebKitLoadEvent e, Client *c);
static void progresschanged(WebKitWebView *v, GParamSpec *ps, Client *c);
static void titlechanged(WebKitWebView *view, GParamSpec *ps, Client *c);
static void mousetargetchanged(WebKitWebView *v, WebKitHitTestResult *h,
                               guint modifiers, Client *c);
static gboolean permissionrequested(WebKitWebView *v,
                                    WebKitPermissionRequest *r, Client *c);
static gboolean decidepolicy(WebKitWebView *v, WebKitPolicyDecision *d,
                             WebKitPolicyDecisionType dt, Client *c);
static void decidenavigation(WebKitPolicyDecision *d, Client *c);
static void decidenewwindow(WebKitPolicyDecision *d, Client *c);
static void decideresource(WebKitPolicyDecision *d, Client *c);
static void insecurecontent(WebKitWebView *v, WebKitInsecureContentEvent e,
                            Client *c);
static void downloadstarted(WebKitWebContext *wc, WebKitDownload *d,
                            Client *c);
static void responsereceived(WebKitDownload *d, GParamSpec *ps, Client *c);
static void download(Client *c, WebKitURIResponse *r);
static gboolean viewusrmsgrcv(WebKitWebView *v, WebKitUserMessage *m,
                              gpointer u);
static void webprocessterminated(WebKitWebView *v,
                                 WebKitWebProcessTerminationReason r,
                                 Client *c);
static void closeview(WebKitWebView *v, Client *c);
static void destroywin(GtkWidget* w, Client *c);

/* Hotkeys */
static void pasteuri(GtkClipboard *clipboard, const char *text, gpointer d);
static void reload(Client *c, const Arg *a);
static void print(Client *c, const Arg *a);
static void showcert(Client *c, const Arg *a);
static void clipboard(Client *c, const Arg *a);
static void zoom(Client *c, const Arg *a);
static void scrollv(Client *c, const Arg *a);
static void scrollh(Client *c, const Arg *a);
static void navigate(Client *c, const Arg *a);
static void stop(Client *c, const Arg *a);
static void toggle(Client *c, const Arg *a);
static void togglefullscreen(Client *c, const Arg *a);
static void togglecookiepolicy(Client *c, const Arg *a);
static void toggleinspector(Client *c, const Arg *a);
static void find(Client *c, const Arg *a);
static void showxid(Client *c, const Arg *a);
static void toggleinsert(Client *c, const Arg *a);
static void openbar(Client *c, const Arg *a);
static void closebar(Client *c);
static void updatebar(Client *c);
static void baractivate(GtkEntry *entry, Client *c);
static gboolean barkeypress(GtkWidget *w, GdkEvent *e, Client *c);

/* Buttons */
static void clicknavigate(Client *c, const Arg *a, WebKitHitTestResult *h);
static void clicknewwindow(Client *c, const Arg *a, WebKitHitTestResult *h);
static void clickexternplayer(Client *c, const Arg *a, WebKitHitTestResult *h);

static char winid[64];
static char togglestats[11];
static char pagestats[2];
static display_context_t display_ctx;
static int showxidflag = 0;
static int cookiepolicy;
Client *clients;
/* Track all web process sockets */
static GArray *webext_sockets = NULL;

/* Userscript support */
static char *surf_fifo;
static GIOChannel *fifo_chan;
static void setup_fifo(Client *c);
static void spawnuserscript(Client *c, const Arg *a);
static void inject_userscripts_early(WebKitUserContentManager *cm, const char *uri);

static GArray *history_entries = NULL;
static char *historyfile;
static void history_add(const char *uri, const char *title);
static void history_load(void);
static void history_filter(Client *c, const char *text);
static void history_select(Client *c, int direction);
static void history_hide(Client *c);
static gboolean bar_update_filter(gpointer data);
static GtkWidget *history_list = NULL;
static GtkWidget *history_scroll = NULL;
static int history_selected = -1;
static void tab_pin(Client *c, const Arg *a);
static gboolean *tab_pins = NULL;
static guint pin_timer = 0;
static gboolean pin_keepalive(gpointer data);

typedef struct {
	char *uri;
	char *title;
	long timestamp;
} HistoryEntry;

#ifdef X11_SUPPORT
static Atom atoms[AtomLast];
static Window embed;
#endif

#ifdef WAYLAND_SUPPORT
static char *instance_id;  // Unique instance identifier
#endif
static GdkDevice *gdkkb;
static char *stylefile;
static const char *useragent;
static Parameter *curconfig;
static int modparams[ParameterLast];
static int spair[2];
char *argv0;

static ParamName loadtransient[] = {
	Certificate,
	CookiePolicies,
	DiskCache,
	DNSPrefetch,
	FileURLsCrossAccess,
	JavaScript,
	LoadImages,
	PreferredLanguages,
	ShowIndicators,
	StrictTLS,
	ParameterLast
};

static ParamName loadcommitted[] = {
//	AccessMicrophone,
//	AccessWebcam,
	CaretBrowsing,
	DarkMode,
	DefaultCharset,
	FontSize,
	Geolocation,
	HideBackground,
	Inspector,
//	KioskMode,
	MediaManualPlay,
	PDFJSviewer,
	RunInFullscreen,
	ScrollBars,
	SiteQuirks,
	SmoothScrolling,
	SpellChecking,
	SpellLanguages,
	Style,
	ZoomLevel,
	ParameterLast
};

static ParamName loadfinished[] = {
	ParameterLast
};

/* configuration, allows nested code to access above variables */
#include "config.h"

void
die(const char *errstr, ...)
{
       va_list ap;

       va_start(ap, errstr);
       vfprintf(stderr, errstr, ap);
       va_end(ap);
       exit(1);
}

void
usage(void)
{
	die("usage: surf [-bBdDfFgGiIkKmMnNsStTvwxX]\n"
	    "[-a cookiepolicies ] [-c cookiefile] [-C stylefile]\n"
	    "[-r scriptfile] [-u useragent] [-z zoomlevel] [uri]\n");
}

void
setup(void)
{
	GIOChannel *gchanin;
	GdkDisplay *gdpy;
	int i, j;

	/* Initialize random seed */
	srand(time(NULL));

	/* Initialize socket tracking */
	webext_sockets = g_array_new(FALSE, FALSE, sizeof(int));

	/* clean up any zombies immediately */
	sigchld(0);
	if (signal(SIGHUP, sighup) == SIG_ERR)
		die("Can't install SIGHUP handler");

	gtk_init(NULL, NULL);
	
	/* Force single web process for all pages */
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
	webkit_web_context_set_process_model(
	    webkit_web_context_get_default(),
	    WEBKIT_PROCESS_MODEL_SHARED_SECONDARY_PROCESS);
	#pragma GCC diagnostic pop

	gdpy = gdk_display_get_default();
	if (!gdpy)
		die("Failed to get GDK display");

	/* Initialize display backend with the GDK display */
	if (display_init_with_gdk_display(&display_ctx, gdpy) != 0)
		die("Failed to initialize display backend");

#ifdef WAYLAND_SUPPORT
	/* Initialize D-Bus for IPC */
	if (display_ctx.backend == DISPLAY_BACKEND_WAYLAND) {
		fprintf(stderr, "Wayland backend detected, skipping D-Bus initialization for now\n");
		// TODO: Re-enable D-Bus when it's stable
		/*
		if (dbus_init() != 0)
			die("Failed to initialize D-Bus");
		if (dbus_setup_filters() != 0)
			die("Failed to setup D-Bus filters");

		// Set up D-Bus callbacks for surf operations
		dbus_set_callbacks(&clients, (void (*)(Client *, const Arg *))navigate, (void (*)(Client *, const Arg *))find);
		*/
	}
#endif

#ifdef X11_SUPPORT
	if (display_ctx.backend == DISPLAY_BACKEND_X11) {
		/* atoms */
		atoms[AtomFind] = XInternAtom(display_ctx.data.x11.dpy, "_SURF_FIND", False);
		atoms[AtomGo] = XInternAtom(display_ctx.data.x11.dpy, "_SURF_GO", False);
		atoms[AtomUri] = XInternAtom(display_ctx.data.x11.dpy, "_SURF_URI", False);
		atoms[AtomUTF8] = XInternAtom(display_ctx.data.x11.dpy, "UTF8_STRING", False);
	}
#endif

	curconfig = defconfig;

	/* dirs and files */
	cookiefile = buildfile(cookiefile);
	historyfile = buildfile("~/.surf/history");
	scriptfile = buildfile(scriptfile);
	certdir    = buildpath(certdir);
	if (curconfig[Ephemeral].val.i)
		cachedir = NULL;
	else
		cachedir   = buildpath(cachedir);

	gdkkb = gdk_seat_get_keyboard(gdk_display_get_default_seat(gdpy));

	if (socketpair(AF_UNIX, SOCK_DGRAM, 0, spair) < 0) {
		fputs("Unable to create sockets\n", stderr);
		spair[0] = spair[1] = -1;
	} else {
		gchanin = g_io_channel_unix_new(spair[0]);
		g_io_channel_set_encoding(gchanin, NULL, NULL);
		g_io_channel_set_flags(gchanin, g_io_channel_get_flags(gchanin)
		                       | G_IO_FLAG_NONBLOCK, NULL);
		g_io_channel_set_close_on_unref(gchanin, TRUE);
	}


	for (i = 0; i < LENGTH(certs); ++i) {
		if (!regcomp(&(certs[i].re), certs[i].regex, REG_EXTENDED)) {
			certs[i].file = g_strconcat(certdir, "/", certs[i].file,
			                            NULL);
		} else {
			fprintf(stderr, "Could not compile regex: %s\n",
			        certs[i].regex);
			certs[i].regex = NULL;
		}
	}

	if (!stylefile) {
		styledir = buildpath(styledir);
		for (i = 0; i < LENGTH(styles); ++i) {
			if (!regcomp(&(styles[i].re), styles[i].regex,
			    REG_EXTENDED)) {
				styles[i].file = g_strconcat(styledir, "/",
				                    styles[i].file, NULL);
			} else {
				fprintf(stderr, "Could not compile regex: %s\n",
				        styles[i].regex);
				styles[i].regex = NULL;
			}
		}
		g_free(styledir);
	} else {
		stylefile = buildfile(stylefile);
	}

	for (i = 0; i < LENGTH(uriparams); ++i) {
		if (regcomp(&(uriparams[i].re), uriparams[i].uri,
		    REG_EXTENDED)) {
			fprintf(stderr, "Could not compile regex: %s\n",
			        uriparams[i].uri);
			uriparams[i].uri = NULL;
			continue;
		}

		/* copy default parameters with higher priority */
		for (j = 0; j < ParameterLast; ++j) {
			if (defconfig[j].prio >= uriparams[i].config[j].prio)
				uriparams[i].config[j] = defconfig[j];
		}
	}
    setup_fifo(NULL);
	pin_timer = g_timeout_add_seconds(5, pin_keepalive, NULL);
}

void
sigchld(int unused)
{
	if (signal(SIGCHLD, sigchld) == SIG_ERR)
		die("Can't install SIGCHLD handler");
	while (waitpid(-1, NULL, WNOHANG) > 0)
		;
}

void
sighup(int unused)
{
	Arg a = { .i = 0 };
	Client *c;

	for (c = clients; c; c = c->next)
		reload(c, &a);
}

char *
buildfile(const char *path)
{
	char *dname, *bname, *bpath, *fpath;
	FILE *f;

	dname = g_path_get_dirname(path);
	bname = g_path_get_basename(path);

	bpath = buildpath(dname);
	g_free(dname);

	fpath = g_build_filename(bpath, bname, NULL);
	g_free(bpath);
	g_free(bname);

	if (!(f = fopen(fpath, "a")))
		die("Could not open file: %s\n", fpath);

	g_chmod(fpath, 0600); /* always */
	fclose(f);

	return fpath;
}

static const char*
getuserhomedir(const char *user)
{
	struct passwd *pw = getpwnam(user);

	if (!pw)
		die("Can't get user %s login information.\n", user);

	return pw->pw_dir;
}

static const char*
getcurrentuserhomedir(void)
{
	const char *homedir;
	const char *user;
	struct passwd *pw;

	homedir = getenv("HOME");
	if (homedir)
		return homedir;

	user = getenv("USER");
	if (user)
		return getuserhomedir(user);

	pw = getpwuid(getuid());
	if (!pw)
		die("Can't get current user home directory\n");

	return pw->pw_dir;
}

char *
buildpath(const char *path)
{
	char *apath, *fpath;

	if (path[0] == '~')
		apath = untildepath(path);
	else
		apath = g_strdup(path);

	/* creating directory */
	if (g_mkdir_with_parents(apath, 0700) < 0)
		die("Could not access directory: %s\n", apath);

	fpath = realpath(apath, NULL);
	g_free(apath);

	return fpath;
}

char *
untildepath(const char *path)
{
       char *apath, *name, *p;
       const char *homedir;

       if (path[1] == '/' || path[1] == '\0') {
               p = (char *)&path[1];
               homedir = getcurrentuserhomedir();
       } else {
               if ((p = strchr(path, '/')))
                       name = g_strndup(&path[1], p - (path + 1));
               else
                       name = g_strdup(&path[1]);

               homedir = getuserhomedir(name);
               g_free(name);
       }
       apath = g_build_filename(homedir, p, NULL);
       return apath;
}

Client *
newclient(Client *rc)
{
	Client *c;

	if (!(c = calloc(1, sizeof(Client))))
		die("Cannot malloc!\n");

	c->next = clients;
	clients = c;

	c->progress = 100;
	c->view = newview(c, rc ? rc->view : NULL);
	c->mode = ModeNormal;

	return c;
}

void
loaduri(Client *c, const Arg *a)
{
	struct stat st;
	char *url, *path, *apath, *encoded;
	const char *uri = a->v;

	if (g_strcmp0(uri, "") == 0)
		return;

	if (g_str_has_prefix(uri, "http://")  ||
	    g_str_has_prefix(uri, "https://") ||
	    g_str_has_prefix(uri, "file://")  ||
	    g_str_has_prefix(uri, "webkit://") ||
	    g_str_has_prefix(uri, "about:")) {
		url = g_strdup(uri);
	} else {
		if (uri[0] == '~')
			apath = untildepath(uri);
		else
			apath = (char *)uri;
		if (!stat(apath, &st) && (path = realpath(apath, NULL))) {
			url = g_strdup_printf("file://%s", path);
			free(path);
		} else if ((strchr(uri, '.') && !strchr(uri, ' ')) ||
		           g_str_has_prefix(uri, "localhost")) {
			/* Looks like a URL */
			url = g_strdup_printf("https://%s", uri);
		} else {
			/* Search query */
			encoded = g_uri_escape_string(uri, NULL, TRUE);
			url = g_strdup_printf(searchengine, encoded);
			g_free(encoded);
		}
		if (apath != uri)
			free(apath);
	}

	setatom(c, AtomUri, url);

	if (strcmp(url, geturi(c)) == 0) {
		reload(c, a);
	} else {
		webkit_web_view_load_uri(c->view, url);
		updatetitle(c);

		/* Emit D-Bus signal for URI change */
#ifdef WAYLAND_SUPPORT
		if (display_ctx.backend == DISPLAY_BACKEND_WAYLAND) {
			dbus_emit_uri_changed(c->instance_id, url);
		}
#endif
	}

	g_free(url);
}

const char *
geturi(Client *c)
{
	const char *uri;

	if (!(uri = webkit_web_view_get_uri(c->view)))
		uri = "about:blank";
	return uri;
}

void
setatom(Client *c, int a, const char *v)
{
#ifdef X11_SUPPORT
	if (display_ctx.backend == DISPLAY_BACKEND_X11 && display_ctx.data.x11.dpy) {
		XChangeProperty(display_ctx.data.x11.dpy, c->xid,
		                atoms[a], atoms[AtomUTF8], 8, PropModeReplace,
		                (unsigned char *)v, strlen(v) + 1);
		XSync(display_ctx.data.x11.dpy, False);
	}
#endif
#ifdef WAYLAND_SUPPORT
	/* TODO: Implement Wayland equivalent of X11 atoms via D-Bus */
	if (display_ctx.backend == DISPLAY_BACKEND_WAYLAND) {
		/* For now, just ignore atom setting on Wayland */
		/* This means some external tool integration won't work */
	}
#endif
}

const char *
getatom(Client *c, int a)
{
#ifdef X11_SUPPORT
	if (display_ctx.backend == DISPLAY_BACKEND_X11 && display_ctx.data.x11.dpy) {
		static char buf[BUFSIZ];
		Atom adummy;
		int idummy;
		unsigned long ldummy;
		unsigned char *p = NULL;

		XSync(display_ctx.data.x11.dpy, False);
		XGetWindowProperty(display_ctx.data.x11.dpy, c->xid,
		                   atoms[a], 0L, BUFSIZ, False, atoms[AtomUTF8],
		                   &adummy, &idummy, &ldummy, &ldummy, &p);
		if (p)
			strncpy(buf, (char *)p, LENGTH(buf) - 1);
		else
			buf[0] = '\0';
		XFree(p);
		return buf;
	}
#endif
#ifdef WAYLAND_SUPPORT
	if (display_ctx.backend == DISPLAY_BACKEND_WAYLAND) {
		/* TODO: Implement Wayland equivalent via D-Bus */
		switch (a) {
			case AtomFind:
				return "";  /* No search term */
			case AtomGo:
				return "about:blank";  /* Default URL */
			case AtomUri:
				return geturi(c);  /* Get current URI */
			default:
				return "about:blank";
		}
	}
#endif
	return "about:blank";  /* Fallback */
}


/* Tab/buffer management */
typedef struct {
	WebKitWebView **views;
	int count;
	int active;
} TabState;

static TabState tabs = { NULL, 0, -1 };

static
void tab_init(Client *c) {
	if (tabs.count > 0)
		return;

	tabs.count = 1;
	tabs.views = g_malloc(sizeof(WebKitWebView *));
	tabs.views[0] = c->view;
	tabs.active = 0;

	tab_pins = g_malloc0(sizeof(gboolean));
}

static void tab_switch_to(Client *c, int index) {
	if (index < 0 || index >= tabs.count || index == tabs.active)
		return;

	if (tabs.views[index] == NULL)
		return;

	/* Hide current */
	if (tabs.active >= 0 && tabs.active < tabs.count &&
	    tabs.views[tabs.active] != NULL) {
		gtk_widget_hide(GTK_WIDGET(tabs.views[tabs.active]));
	}

	/* Show new */
	tabs.active = index;
	c->view = tabs.views[index];
	c->pageid = webkit_web_view_get_page_id(c->view);
	gtk_widget_show(GTK_WIDGET(c->view));
	gtk_widget_grab_focus(GTK_WIDGET(c->view));

	c->mode = ModeNormal;
	gtk_widget_set_can_focus(c->statentry, FALSE);
	gtk_editable_set_editable(GTK_EDITABLE(c->statentry), FALSE);

	c->title = webkit_web_view_get_title(c->view);
	c->progress = webkit_web_view_get_estimated_load_progress(c->view) * 100;
	updatetitle(c);
}

void
tab_new(Client *c, const Arg *a) {
	WebKitWebView *v;

	tab_init(c);

	v = newview(c, c->view);

	gtk_box_pack_start(GTK_BOX(c->vbox), GTK_WIDGET(v), TRUE, TRUE, 0);
	gtk_box_reorder_child(GTK_BOX(c->vbox), GTK_WIDGET(v), 0);

	gtk_widget_hide(GTK_WIDGET(tabs.views[tabs.active]));

	tabs.count++;
	tabs.views = g_realloc(tabs.views, tabs.count * sizeof(WebKitWebView *));
	tabs.views[tabs.count - 1] = v;

	/* Grow pins array */
	tab_pins = g_realloc(tab_pins, tabs.count * sizeof(gboolean));
	tab_pins[tabs.count - 1] = FALSE;

	tabs.active = tabs.count - 1;

	c->view = v;
	c->pageid = webkit_web_view_get_page_id(v);
	c->title = NULL;
	c->progress = 100;
	c->mode = ModeNormal;

	gtk_widget_show_all(GTK_WIDGET(v));
	webkit_web_view_load_uri(v, "about:blank");

	gtk_widget_set_can_focus(c->statentry, FALSE);
	gtk_editable_set_editable(GTK_EDITABLE(c->statentry), FALSE);
	gtk_widget_grab_focus(GTK_WIDGET(v));
	updatetitle(c);

	if (a && a->i == 0) {
		Arg bar = { .i = 0 };
		openbar(c, &bar);
	}
}

void
tab_close(Client *c, const Arg *a) {
	int idx;

	if (tabs.count <= 1) {
		gtk_widget_destroy(c->win);
		return;
	}

	idx = tabs.active;

	WebKitWebView *dead = tabs.views[idx];

	webkit_web_view_stop_loading(dead);
	gtk_widget_hide(GTK_WIDGET(dead));

	for (int i = idx; i < tabs.count - 1; i++) {
		tabs.views[i] = tabs.views[i + 1];
		if (tab_pins)
			tab_pins[i] = tab_pins[i + 1];
	}
	tabs.count--;
	tabs.views = g_realloc(tabs.views, tabs.count * sizeof(WebKitWebView *));
	tab_pins = g_realloc(tab_pins, tabs.count * sizeof(gboolean));

	int new_idx = idx < tabs.count ? idx : tabs.count - 1;
	tabs.active = -1;
	tab_switch_to(c, new_idx);

	g_signal_handlers_disconnect_matched(dead,
		G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, c);

	webkit_web_view_load_uri(dead, "about:blank");
}

void
tab_next(Client *c, const Arg *a)
{
	tab_init(c);
	if (tabs.count <= 1)
		return;
	int next = (tabs.active + 1) % tabs.count;
	tab_switch_to(c, next);
}

void
tab_prev(Client *c, const Arg *a)
{
	tab_init(c);
	if (tabs.count <= 1)
		return;
	int prev = (tabs.active - 1 + tabs.count) % tabs.count;
	tab_switch_to(c, prev);
}
/* Hint label characters (home row keys) */
static const char *hintkeys = "asdfghjkl";

typedef struct {
	char *label;
	char *url;
	void *element;  /* WebKitDOMElement */
	int x, y;       /* Position for overlay */
} Hint;

typedef struct {
	GArray *hints;
	char *input;
	HintMode mode;
	int active;
	guint64 pageid;
} HintState;

static HintState hintstate = {0};

/* Generate hint labels - all same length to avoid ambiguity */
static int
hint_label_length(int count)
{
	int len = strlen(hintkeys);
	int digits = 1;
	int capacity = len;

	while (capacity < count) {
		digits++;
		capacity *= len;
	}

	return digits;
}

static char *
gen_hint_label(int index, int total)
{
	char *label;
	int len = strlen(hintkeys);
	int digits = hint_label_length(total);

	label = g_malloc0(digits + 1);

	for (int i = digits - 1; i >= 0; i--) {
		label[i] = hintkeys[index % len];
		index /= len;
	}

	return label;
}

/* Request hint data from web extension */
static void
request_hints_from_extension(Client *c)
{
	WebKitUserMessage *msg;
	msg = webkit_user_message_new("hints-find-links", NULL);
	webkit_web_view_send_message_to_page(c->view, msg, NULL, NULL, NULL);
}

/* Cleanup hint state */
void
hints_cleanup(Client *c) {
	if (!hintstate.active)
		return;
	
	if (hintstate.hints) {
		for (guint i = 0; i < hintstate.hints->len; i++) {
			Hint *h = &g_array_index(hintstate.hints, Hint, i);
			g_free(h->label);
			g_free(h->url);
		}
		g_array_free(hintstate.hints, TRUE);
		hintstate.hints = NULL;
	}
	
	g_free(hintstate.input);
	hintstate.input = NULL;
	
	/* Only send clear if we're still on the same page that had hints */
	if (hintstate.active && hintstate.pageid == webkit_web_view_get_page_id(c->view)) {
		WebKitUserMessage *msg = webkit_user_message_new("hints-clear", NULL);
		webkit_web_view_send_message_to_page(c->view, msg, NULL, NULL, NULL);
	}
	
	hintstate.active = 0;
	
	/* Return to normal mode */
	c->mode = ModeNormal;
	updatetitle(c);
}

/* Start hint mode */
void
hints_start(Client *c, const Arg *a)
{
	if (hintstate.active)
		hints_cleanup(c);

	hintstate.mode = a->i;
	hintstate.active = 1;
	hintstate.pageid = c->pageid;
	hintstate.hints = g_array_new(FALSE, TRUE, sizeof(Hint));
	hintstate.input = g_strdup("");

	c->mode = ModeHint;

	request_hints_from_extension(c);
	updatetitle(c);
}

/* Filter hints based on input */
static void
filter_hints(Client *c)
{
	GVariantBuilder builder;

	g_variant_builder_init(&builder, G_VARIANT_TYPE("a(ssii)"));

	for (guint i = 0; i < hintstate.hints->len; i++) {
		Hint *h = &g_array_index(hintstate.hints, Hint, i);
		if (g_str_has_prefix(h->label, hintstate.input)) {
			g_variant_builder_add(&builder, "(ssii)",
				h->label, h->url, h->x, h->y);
		}
	}

	GVariant *hints_data = g_variant_builder_end(&builder);
	WebKitUserMessage *msg = webkit_user_message_new("hints-update", hints_data);
	webkit_web_view_send_message_to_page(c->view, msg, NULL, NULL, NULL);
}

/* Follow a hint */
static void follow_hint(Client *c, const char *label) {
	Hint *target = NULL;

	for (guint i = 0; i < hintstate.hints->len; i++) {
		Hint *h = &g_array_index(hintstate.hints, Hint, i);
		if (strcmp(h->label, label) == 0) {
			target = h;
			break;
		}
	}

	if (!target) {
		hints_cleanup(c);
		return;
	}

	/* Check if this is a clickable element (not a URL) */
	if (g_str_has_prefix(target->url, "[click:")) {
		int cx, cy;
		if (sscanf(target->url, "[click:%d,%d]", &cx, &cy) == 2) {
			GVariant *data = g_variant_new("(ii)", cx, cy);
			WebKitUserMessage *msg = webkit_user_message_new("hints-click", data);
			webkit_web_view_send_message_to_page(c->view, msg, NULL, NULL, NULL);
		}
		hints_cleanup(c);
		return;
	}

	Arg arg;
	switch (hintstate.mode) {
	case HintModeLink:
		arg.v = target->url;
		loaduri(c, &arg);
		break;
	case HintModeNewWindow:
		{
			WebKitWebView *old_view = c->view;

			WebKitUserMessage *clear_msg = webkit_user_message_new("hints-clear", NULL);
			webkit_web_view_send_message_to_page(old_view, clear_msg, NULL, NULL, NULL);

			tab_init(c);
			tab_new(c, &(Arg){ .i = 1 }); /* .i = 1 means skip openbar */
			arg.v = target->url;
			loaduri(c, &arg);
		}
		break;
	case HintModeYank:
		gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_PRIMARY),
		                       target->url, -1);
		break;
	}

	hints_cleanup(c);
}

/* Handle keypress in hint mode */
gboolean
hints_keypress(Client *c, GdkEventKey *e)
{
	char key;
	char *newinput;
	int label_len;

	if (!hintstate.active || hintstate.pageid != c->pageid)
		return FALSE;

	/* Escape cancels */
	if (e->keyval == GDK_KEY_Escape) {
		hints_cleanup(c);
		return TRUE;
	}

	/* Backspace removes character */
	if (e->keyval == GDK_KEY_BackSpace) {
		int len = strlen(hintstate.input);
		if (len > 0) {
			hintstate.input[len - 1] = '\0';
			filter_hints(c);
		} else {
			hints_cleanup(c);
		}
		return TRUE;
	}

	/* Check if key is a hint key */
	key = gdk_keyval_to_lower(e->keyval);
	if (!strchr(hintkeys, key))
		return TRUE;

	/* Add to input */
	newinput = g_strdup_printf("%s%c", hintstate.input, key);
	g_free(hintstate.input);
	hintstate.input = newinput;

	/* Get the expected label length (all labels are same length) */
	if (hintstate.hints->len == 0) {
		hints_cleanup(c);
		return TRUE;
	}

	label_len = strlen(g_array_index(hintstate.hints, Hint, 0).label);

	/* Count matching hints */
	int matches = 0;
	char *matched_label = NULL;

	for (guint i = 0; i < hintstate.hints->len; i++) {
		Hint *h = &g_array_index(hintstate.hints, Hint, i);
		if (g_str_has_prefix(h->label, hintstate.input)) {
			matches++;
			matched_label = h->label;
		}
	}

	if (matches == 0) {
		/* No matches */
		hints_cleanup(c);
	} else if ((int)strlen(hintstate.input) == label_len && matches == 1) {
		/* Full label typed - follow it */
		follow_hint(c, matched_label);
	} else {
		/* Still typing - filter display */
		filter_hints(c);
	}

	return TRUE;
}

/* Receive hints data from extension */
void
hints_receive_data(Client *c, GVariant *data)
{
	GVariantIter iter;
	const gchar *url;
	gint x, y, width, height;
	int index = 0;
	int total;

	total = g_variant_n_children(data);

	if (total == 0) {
		hints_cleanup(c);
		return;
	}

	g_variant_iter_init(&iter, data);

	while (g_variant_iter_loop(&iter, "(siiii)", &url, &x, &y, &width, &height)) {
		Hint hint;
		hint.label = gen_hint_label(index++, total);
		hint.url = g_strdup(url);
		hint.x = x;
		hint.y = y;
		g_array_append_val(hintstate.hints, hint);
	}

	filter_hints(c);
}

void
updatetitle(Client *c) {
	char *title;
	const char *name;
	const char *pin;

	if (c->overtitle)
		name = c->overtitle;
	else if (c->title)
		name = c->title;
	else
		name = "";

	pin = (tab_pins && tabs.active >= 0 && tabs.active < tabs.count &&
	       tab_pins[tabs.active]) ? "[P]" : "";

	if (tabs.count > 1) {
		title = g_strdup_printf("[%d/%d]%s %s",
			tabs.active + 1, tabs.count, pin, name);
	} else {
		title = g_strdup_printf("%s%s", name, pin);
	}

	gtk_window_set_title(GTK_WINDOW(c->win), title);
	g_free(title);

	updatebar(c);
}

void
gettogglestats(Client *c)
{
	togglestats[0] = cookiepolicy_set(cookiepolicy_get());
	togglestats[1] = curconfig[CaretBrowsing].val.i ?   'C' : 'c';
	togglestats[2] = curconfig[Geolocation].val.i ?     'G' : 'g';
	togglestats[3] = curconfig[DiskCache].val.i ?       'D' : 'd';
	togglestats[4] = curconfig[LoadImages].val.i ?      'I' : 'i';
	togglestats[5] = curconfig[JavaScript].val.i ?      'S' : 's';
	togglestats[6] = curconfig[Style].val.i ?           'M' : 'm';
	togglestats[8] = curconfig[Certificate].val.i ?     'X' : 'x';
	togglestats[9] = curconfig[StrictTLS].val.i ?       'T' : 't';
}

void
getpagestats(Client *c)
{
	if (c->https)
		pagestats[0] = (c->tlserr || c->insecure) ?  'U' : 'T';
	else
		pagestats[0] = '-';
	pagestats[1] = '\0';
}

WebKitCookieAcceptPolicy
cookiepolicy_get(void)
{
	switch (((char *)curconfig[CookiePolicies].val.v)[cookiepolicy]) {
	case 'a':
		return WEBKIT_COOKIE_POLICY_ACCEPT_NEVER;
	case '@':
		return WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY;
	default: /* fallthrough */
	case 'A':
		return WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS;
	}
}

char
cookiepolicy_set(const WebKitCookieAcceptPolicy p)
{
	switch (p) {
	case WEBKIT_COOKIE_POLICY_ACCEPT_NEVER:
		return 'a';
	case WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY:
		return '@';
	default: /* fallthrough */
	case WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS:
		return 'A';
	}
}

void
seturiparameters(Client *c, const char *uri, ParamName *params)
{
	Parameter *config, *uriconfig = NULL;
	int i, p;

	for (i = 0; i < LENGTH(uriparams); ++i) {
		if (uriparams[i].uri &&
		    !regexec(&(uriparams[i].re), uri, 0, NULL, 0)) {
			uriconfig = uriparams[i].config;
			break;
		}
	}

	curconfig = uriconfig ? uriconfig : defconfig;

	for (i = 0; (p = params[i]) != ParameterLast; ++i) {
		switch(p) {
		default: /* FALLTHROUGH */
			if (!(defconfig[p].prio < curconfig[p].prio ||
			    defconfig[p].prio < modparams[p]))
				continue;
		case Certificate:
		case CookiePolicies:
		case Style:
			setparameter(c, 0, p, &curconfig[p].val);
		}
	}
}

void
setparameter(Client *c, int refresh, ParamName p, const Arg *a)
{
	GdkRGBA bgcolor = { 0 };

	modparams[p] = curconfig[p].prio;

	switch (p) {
	case AccessMicrophone:
		return; /* do nothing */
	case AccessWebcam:
		return; /* do nothing */
	case CaretBrowsing:
		webkit_settings_set_enable_caret_browsing(c->settings, a->i);
		refresh = 0;
		break;
	case Certificate:
		if (a->i)
			setcert(c, geturi(c));
		return; /* do not update */
	case CookiePolicies:
		webkit_cookie_manager_set_accept_policy(
		    webkit_web_context_get_cookie_manager(c->context),
		    cookiepolicy_get());
		refresh = 0;
		break;
	case DarkMode:
		g_object_set(gtk_settings_get_default(),
		             "gtk-application-prefer-dark-theme", a->i, NULL);
		return;
	case DiskCache:
		webkit_web_context_set_cache_model(c->context, a->i ?
		    WEBKIT_CACHE_MODEL_WEB_BROWSER :
		    WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);
		return; /* do not update */
	case DefaultCharset:
		webkit_settings_set_default_charset(c->settings, a->v);
		return; /* do not update */
	case DNSPrefetch:
		webkit_settings_set_enable_dns_prefetching(c->settings, a->i);
		return; /* do not update */
	case FileURLsCrossAccess:
		webkit_settings_set_allow_file_access_from_file_urls(
		    c->settings, a->i);
		webkit_settings_set_allow_universal_access_from_file_urls(
		    c->settings, a->i);
		return; /* do not update */
	case FontSize:
		webkit_settings_set_default_font_size(c->settings, a->i);
		return; /* do not update */
	case Geolocation:
		refresh = 0;
		break;
	case HideBackground:
		if (a->i)
			webkit_web_view_set_background_color(c->view, &bgcolor);
		return; /* do not update */
	case Inspector:
		webkit_settings_set_enable_developer_extras(c->settings, a->i);
		return; /* do not update */
	case JavaScript:
		webkit_settings_set_enable_javascript(c->settings, a->i);
		break;
	case KioskMode:
		return; /* do nothing */
	case LoadImages:
		webkit_settings_set_auto_load_images(c->settings, a->i);
		break;
	case MediaManualPlay:
		webkit_settings_set_media_playback_requires_user_gesture(
		    c->settings, a->i);
		break;
	case PDFJSviewer:
		return; /* do nothing */
	case PreferredLanguages:
		return; /* do nothing */
	case RunInFullscreen:
		return; /* do nothing */
	case ScrollBars:
		/* Disabled until we write some WebKitWebExtension for
		 * manipulating the DOM directly.
		enablescrollbars = !enablescrollbars;
		evalscript(c, "document.documentElement.style.overflow = '%s'",
		    enablescrollbars ? "auto" : "hidden");
		*/
		return; /* do not update */
	case ShowIndicators:
		break;
	case SmoothScrolling:
		webkit_settings_set_enable_smooth_scrolling(c->settings, a->i);
		return; /* do not update */
	case SiteQuirks:
		webkit_settings_set_enable_site_specific_quirks(
		    c->settings, a->i);
		break;
	case SpellChecking:
		webkit_web_context_set_spell_checking_enabled(
		    c->context, a->i);
		return; /* do not update */
	case SpellLanguages:
		return; /* do nothing */
	case StrictTLS:
		webkit_website_data_manager_set_tls_errors_policy(
		    webkit_web_view_get_website_data_manager(c->view), a->i ?
		    WEBKIT_TLS_ERRORS_POLICY_FAIL :
		    WEBKIT_TLS_ERRORS_POLICY_IGNORE);
		break;
	case Style:
		webkit_user_content_manager_remove_all_style_sheets(
		    webkit_web_view_get_user_content_manager(c->view));
		if (a->i)
			setstyle(c, getstyle(geturi(c)));
		refresh = 0;
		break;
	case WebGL:
		webkit_settings_set_enable_webgl(c->settings, a->i);
		break;
	case ZoomLevel:
		webkit_web_view_set_zoom_level(c->view, a->f);
		return; /* do not update */
	default:
		return; /* do nothing */
	}

	updatetitle(c);
	if (refresh)
		reload(c, a);
}

const char *
getcert(const char *uri)
{
	int i;

	for (i = 0; i < LENGTH(certs); ++i) {
		if (certs[i].regex &&
		    !regexec(&(certs[i].re), uri, 0, NULL, 0))
			return certs[i].file;
	}

	return NULL;
}

void
setcert(Client *c, const char *uri)
{
	const char *file = getcert(uri);
	char *host;
	GTlsCertificate *cert;

	if (!file)
		return;

	if (!(cert = g_tls_certificate_new_from_file(file, NULL))) {
		fprintf(stderr, "Could not read certificate file: %s\n", file);
		return;
	}

	if ((uri = strstr(uri, "https://"))) {
		uri += sizeof("https://") - 1;
		host = g_strndup(uri, strchr(uri, '/') - uri);
		webkit_web_context_allow_tls_certificate_for_host(c->context,
		    cert, host);
		g_free(host);
	}

	g_object_unref(cert);

}

const char *
getstyle(const char *uri)
{
	int i;

	if (stylefile)
		return stylefile;

	for (i = 0; i < LENGTH(styles); ++i) {
		if (styles[i].regex &&
		    !regexec(&(styles[i].re), uri, 0, NULL, 0))
			return styles[i].file;
	}

	return "";
}

void
setstyle(Client *c, const char *file)
{
	gchar *style;

	if (!g_file_get_contents(file, &style, NULL, NULL)) {
		fprintf(stderr, "Could not read style file: %s\n", file);
		return;
	}

	webkit_user_content_manager_add_style_sheet(
	    webkit_web_view_get_user_content_manager(c->view),
	    webkit_user_style_sheet_new(style,
	    WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
	    WEBKIT_USER_STYLE_LEVEL_USER,
	    NULL, NULL));

	g_free(style);
}

void
runscript(Client *c)
{
	gchar *script;
	gsize l;

	if (g_file_get_contents(scriptfile, &script, &l, NULL) && l)
		evalscript(c, "%s", script);
	g_free(script);
}

void
evalscript(Client *c, const char *jsstr, ...)
{
	va_list ap;
	gchar *script;

	va_start(ap, jsstr);
	script = g_strdup_vprintf(jsstr, ap);
	va_end(ap);

	webkit_web_view_evaluate_javascript(c->view, script, -1,
	    NULL, NULL, NULL, NULL, NULL);
	g_free(script);
}

void
updatewinid(Client *c)
{
#ifdef X11_SUPPORT
	if (display_ctx.backend == DISPLAY_BACKEND_X11) {
		snprintf(winid, LENGTH(winid), "%lu", c->xid);
	} else
#endif
#ifdef WAYLAND_SUPPORT
	if (display_ctx.backend == DISPLAY_BACKEND_WAYLAND) {
		snprintf(winid, LENGTH(winid), "%s", c->instance_id);
	} else
#endif
	{
		snprintf(winid, LENGTH(winid), "%s", c->instance_id);
	}
}

void
handleplumb(Client *c, const char *uri)
{
	Arg a = (Arg)PLUMB(uri);
	spawn(c, &a);
}

void
newwindow(Client *c, const Arg *a, int noembed)
{
	int i = 0;
	char tmp[64];
	const char *cmd[29], *uri;
	const Arg arg = { .v = cmd };

	cmd[i++] = argv0;
	cmd[i++] = "-a";
	cmd[i++] = curconfig[CookiePolicies].val.v;
	cmd[i++] = curconfig[ScrollBars].val.i ? "-B" : "-b";
	if (cookiefile && g_strcmp0(cookiefile, "")) {
		cmd[i++] = "-c";
		cmd[i++] = cookiefile;
	}
	if (stylefile && g_strcmp0(stylefile, "")) {
		cmd[i++] = "-C";
		cmd[i++] = stylefile;
	}
	cmd[i++] = curconfig[DiskCache].val.i ? "-D" : "-d";
	/* Embedding support removed - always create toplevel windows */
	cmd[i++] = curconfig[RunInFullscreen].val.i ? "-F" : "-f" ;
	cmd[i++] = curconfig[Geolocation].val.i ?     "-G" : "-g" ;
	cmd[i++] = curconfig[LoadImages].val.i ?      "-I" : "-i" ;
	cmd[i++] = curconfig[KioskMode].val.i ?       "-K" : "-k" ;
	cmd[i++] = curconfig[Style].val.i ?           "-M" : "-m" ;
	cmd[i++] = curconfig[Inspector].val.i ?       "-N" : "-n" ;
	if (scriptfile && g_strcmp0(scriptfile, "")) {
		cmd[i++] = "-r";
		cmd[i++] = scriptfile;
	}
	cmd[i++] = curconfig[JavaScript].val.i ? "-S" : "-s";
	cmd[i++] = curconfig[StrictTLS].val.i ? "-T" : "-t";
	if (fulluseragent && g_strcmp0(fulluseragent, "")) {
		cmd[i++] = "-u";
		cmd[i++] = fulluseragent;
	}
	if (showxid)
		cmd[i++] = "-w";
	cmd[i++] = curconfig[Certificate].val.i ? "-X" : "-x" ;
	/* do not keep zoom level */
	cmd[i++] = "--";
	if ((uri = a->v))
		cmd[i++] = uri;
	cmd[i] = NULL;

	spawn(c, &arg);
}

void
spawn(Client *c, const Arg *a)
{
	if (fork() == 0) {
#ifdef X11_SUPPORT
		if (display_ctx.backend == DISPLAY_BACKEND_X11 && display_ctx.data.x11.dpy)
			close(ConnectionNumber(display_ctx.data.x11.dpy));
#endif
		close(spair[0]);
		close(spair[1]);
		setsid();
		execvp(((char **)a->v)[0], (char **)a->v);
		fprintf(stderr, "%s: execvp %s", argv0, ((char **)a->v)[0]);
		perror(" failed");
		exit(1);
	}
}

void
destroyclient(Client *c)
{
	Client *p;

	webkit_web_view_stop_loading(c->view);
	/* Not needed, has already been called
	gtk_widget_destroy(c->win);
	 */

	for (p = clients; p && p->next != c; p = p->next)
		;
	if (p)
		p->next = c->next;
	else
		clients = c->next;
	free(c);
}

void
cleanup(void)
{
	while (clients)
		destroyclient(clients);

	close(spair[0]);
	close(spair[1]);
	g_free(cookiefile);
	g_free(scriptfile);
	g_free(stylefile);
	g_free(cachedir);
#ifdef WAYLAND_SUPPORT
	dbus_cleanup();
#endif
	display_cleanup(&display_ctx);
    if (surf_fifo) {
        unlink(surf_fifo);
        g_free(surf_fifo);
    }
	g_free(historyfile);
	if (pin_timer)
		g_source_remove(pin_timer);
}

/* Function to handle display backend failures */
static void
handle_display_error(const char *operation)
{
	fprintf(stderr, "Display error during %s\n", operation);

	/* Try to fallback to alternative backend if available */
#ifdef X11_SUPPORT
#ifdef WAYLAND_SUPPORT
	if (display_ctx.backend == DISPLAY_BACKEND_WAYLAND) {
		fprintf(stderr, "Attempting fallback to X11...\n");
		if (display_init(&display_ctx) == 0) {
			fprintf(stderr, "Successfully fallbacked to X11\n");
			return;
		}
	}
#endif
#endif

	die("No available display backends");
}

/* Global instance ID for the browser process */
static char *browser_instance_id = NULL;

/* Generate unique instance ID for a client */
static void
generate_instance_id(Client *c)
{
	snprintf(c->instance_id, sizeof(c->instance_id),
	         "surf-%ld-%d", (long)getpid(), rand());
}

/* Get browser instance ID */
static const char *
get_instance_id(void)
{
	if (!browser_instance_id) {
		browser_instance_id = g_strdup_printf("surf-browser-%ld-%d",
		                                     (long)getpid(), rand());
	}
	return browser_instance_id;
}

/* Function to get window/instance ID via stdout */
static void
showxid(Client *c, const Arg *arg)
{
#ifdef X11_SUPPORT
	if (display_ctx.backend == DISPLAY_BACKEND_X11) {
		printf("%lu\n", c->xid);
	} else
#endif
	{
		puts(c->instance_id);
	}
}

WebKitWebView *
newview(Client *c, WebKitWebView *rv)
{
	WebKitWebView *v;
	WebKitSettings *settings;
	WebKitWebContext *context;
	WebKitCookieManager *cookiemanager;
	WebKitUserContentManager *contentmanager;

	/* Webview */
	if (rv) {
		v = WEBKIT_WEB_VIEW(webkit_web_view_new_with_related_view(rv));
		context = webkit_web_view_get_context(v);
		settings = webkit_web_view_get_settings(v);
	} else {
		settings = webkit_settings_new_with_settings(
		   "allow-file-access-from-file-urls", curconfig[FileURLsCrossAccess].val.i,
		   "allow-universal-access-from-file-urls", curconfig[FileURLsCrossAccess].val.i,
		   "auto-load-images", curconfig[LoadImages].val.i,
		   "default-charset", curconfig[DefaultCharset].val.v,
		   "default-font-size", curconfig[FontSize].val.i,
		   "enable-caret-browsing", curconfig[CaretBrowsing].val.i,
		   "enable-developer-extras", curconfig[Inspector].val.i,
		   "enable-dns-prefetching", curconfig[DNSPrefetch].val.i,
		   "enable-html5-database", curconfig[DiskCache].val.i,
		   "enable-html5-local-storage", curconfig[DiskCache].val.i,
		   "enable-javascript", curconfig[JavaScript].val.i,
		   "enable-site-specific-quirks", curconfig[SiteQuirks].val.i,
		   "enable-smooth-scrolling", curconfig[SmoothScrolling].val.i,
		   "enable-webgl", curconfig[WebGL].val.i,
		   "media-playback-requires-user-gesture", curconfig[MediaManualPlay].val.i,
		   NULL);
/* For more interesting settings, have a look at
 * http://webkitgtk.org/reference/webkit2gtk/stable/WebKitSettings.html */

		if (strcmp(fulluseragent, "")) {
			webkit_settings_set_user_agent(settings, fulluseragent);
		} else if (surfuseragent) {
			webkit_settings_set_user_agent_with_application_details(
			    settings, "Surf", VERSION);
		}
		useragent = webkit_settings_get_user_agent(settings);

		contentmanager = webkit_user_content_manager_new();
        inject_userscripts_early(contentmanager, "");  /* must be here */

		if (curconfig[Ephemeral].val.i) {
			context = webkit_web_context_new_ephemeral();
		} else {
			context = webkit_web_context_new_with_website_data_manager(
			          webkit_website_data_manager_new(
			          "base-cache-directory", cachedir,
			          "base-data-directory", cachedir,
			          NULL));
		}

		cookiemanager = webkit_web_context_get_cookie_manager(context);

		/* TLS */
		webkit_website_data_manager_set_tls_errors_policy(
		    webkit_web_context_get_website_data_manager(context),
		    curconfig[StrictTLS].val.i ? WEBKIT_TLS_ERRORS_POLICY_FAIL :
		    WEBKIT_TLS_ERRORS_POLICY_IGNORE);
		/* disk cache */
		webkit_web_context_set_cache_model(context,
		    curconfig[DiskCache].val.i ? WEBKIT_CACHE_MODEL_WEB_BROWSER :
		    WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);

		/* Currently only works with text file to be compatible with curl */
		if (!curconfig[Ephemeral].val.i)
			webkit_cookie_manager_set_persistent_storage(cookiemanager,
			    cookiefile, WEBKIT_COOKIE_PERSISTENT_STORAGE_TEXT);
		/* cookie policy */
		webkit_cookie_manager_set_accept_policy(cookiemanager,
		    cookiepolicy_get());
		/* languages */
		webkit_web_context_set_preferred_languages(context,
		    curconfig[PreferredLanguages].val.v);
		webkit_web_context_set_spell_checking_languages(context,
		    curconfig[SpellLanguages].val.v);
		webkit_web_context_set_spell_checking_enabled(context,
		    curconfig[SpellChecking].val.i);

		g_signal_connect(G_OBJECT(context), "download-started",
		                 G_CALLBACK(downloadstarted), c);
		g_signal_connect(G_OBJECT(context), "initialize-web-extensions",
		                 G_CALLBACK(initwebextensions), c);

		v = g_object_new(WEBKIT_TYPE_WEB_VIEW,
		    "settings", settings,
		    "user-content-manager", contentmanager,
		    "web-context", context,
		    NULL);
	}

	g_signal_connect(G_OBJECT(v), "notify::estimated-load-progress",
			 G_CALLBACK(progresschanged), c);
	g_signal_connect(G_OBJECT(v), "notify::title",
			 G_CALLBACK(titlechanged), c);
	g_signal_connect(G_OBJECT(v), "button-release-event",
			 G_CALLBACK(buttonreleased), c);
	g_signal_connect(G_OBJECT(v), "close",
			G_CALLBACK(closeview), c);
	g_signal_connect(G_OBJECT(v), "create",
			 G_CALLBACK(createview), c);
	g_signal_connect(G_OBJECT(v), "decide-policy",
			 G_CALLBACK(decidepolicy), c);
	g_signal_connect(G_OBJECT(v), "insecure-content-detected",
			 G_CALLBACK(insecurecontent), c);
	g_signal_connect(G_OBJECT(v), "load-failed-with-tls-errors",
			 G_CALLBACK(loadfailedtls), c);
	g_signal_connect(G_OBJECT(v), "load-changed",
			 G_CALLBACK(loadchanged), c);
	g_signal_connect(G_OBJECT(v), "mouse-target-changed",
			 G_CALLBACK(mousetargetchanged), c);
	g_signal_connect(G_OBJECT(v), "permission-request",
			 G_CALLBACK(permissionrequested), c);
	g_signal_connect(G_OBJECT(v), "ready-to-show",
			 G_CALLBACK(showview), c);
	g_signal_connect(G_OBJECT(v), "user-message-received",
			 G_CALLBACK(viewusrmsgrcv), c);
	g_signal_connect(G_OBJECT(v), "web-process-terminated",
			 G_CALLBACK(webprocessterminated), c);

	c->context = context;
	c->settings = settings;

	setparameter(c, 0, DarkMode, &curconfig[DarkMode].val);

	return v;
}

void
initwebextensions(WebKitWebContext *wc, Client *c)
{
	webkit_web_context_set_web_extensions_directory(wc, WEBEXTDIR);
}

GtkWidget *
createview(WebKitWebView *v, WebKitNavigationAction *a, Client *c)
{
	Client *n;

	switch (webkit_navigation_action_get_navigation_type(a)) {
	case WEBKIT_NAVIGATION_TYPE_OTHER: /* fallthrough */
		/*
		 * popup windows of type "other" are almost always triggered
		 * by user gesture, so inverse the logic here
		 */
/* instead of this, compare destination uri to mouse-over uri for validating window */
		if (webkit_navigation_action_is_user_gesture(a))
			return NULL;
	case WEBKIT_NAVIGATION_TYPE_LINK_CLICKED: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_FORM_SUBMITTED: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_BACK_FORWARD: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_RELOAD: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_FORM_RESUBMITTED:
		n = newclient(c);
		break;
	default:
		return NULL;
	}

	return GTK_WIDGET(n->view);
}

gboolean
buttonreleased(GtkWidget *w, GdkEvent *e, Client *c)
{
	WebKitHitTestResultContext element;
	int i;

	element = webkit_hit_test_result_get_context(c->mousepos);

	/* Auto-enter insert mode when clicking editable elements */
	if (element & OnEdit) {
		c->mode = ModeInsert;
		updatetitle(c);
	}

	for (i = 0; i < LENGTH(buttons); ++i) {
		if (element & buttons[i].target &&
		    e->button.button == buttons[i].button &&
		    CLEANMASK(e->button.state) == CLEANMASK(buttons[i].mask) &&
		    buttons[i].func) {
			buttons[i].func(c, &buttons[i].arg, c->mousepos);
			return buttons[i].stopevent;
		}
	}

	return FALSE;
}

GdkFilterReturn
processx(GdkXEvent *e, GdkEvent *event, gpointer d)
{
#ifdef X11_SUPPORT
	Client *c = (Client *)d;
	XPropertyEvent *ev;
	Arg a;

	if (((XEvent *)e)->type == PropertyNotify) {
		ev = &((XEvent *)e)->xproperty;
		if (ev->state == PropertyNewValue) {
			if (ev->atom == atoms[AtomFind]) {
				find(c, NULL);

				return GDK_FILTER_REMOVE;
			} else if (ev->atom == atoms[AtomGo]) {
				a.v = getatom(c, AtomGo);
				loaduri(c, &a);

				return GDK_FILTER_REMOVE;
			}
		}
	}
#endif
	return GDK_FILTER_CONTINUE;
}

gboolean
winevent(GtkWidget *w, GdkEvent *e, Client *c)
{
	int i;

	switch (e->type) {
	case GDK_ENTER_NOTIFY:
		c->overtitle = c->targeturi;
		updatetitle(c);
		break;
	case GDK_KEY_PRESS:
		if (curconfig[KioskMode].val.i)
			break;

		/* Command mode: keys go to the bar entry */
		if (c->mode == ModeCommand)
			return FALSE; /* GTK handles entry input */

		/* Insert mode: pass everything except Escape */
		if (c->mode == ModeInsert) {
			if (e->key.keyval == GDK_KEY_Escape) {
				c->mode = ModeNormal;
				gtk_widget_grab_focus(GTK_WIDGET(c->view));
				updatetitle(c);
				return TRUE;
			}
			return FALSE; /* pass all keys to webpage */
		}

        if (c->mode == ModeHint) {
            return hints_keypress(c, &e->key);
        }

		/* Normal mode: process keybinds */
		for (i = 0; i < LENGTH(keys); ++i) {
			if (gdk_keyval_to_lower(e->key.keyval) ==
			    keys[i].keyval &&
			    CLEANMASK(e->key.state) == keys[i].mod &&
			    keys[i].func) {
				updatewinid(c);
				keys[i].func(c, &(keys[i].arg));
				return TRUE;
			}
		}
		break;
	case GDK_LEAVE_NOTIFY:
		c->overtitle = NULL;
		updatetitle(c);
		break;
	case GDK_WINDOW_STATE:
		if (e->window_state.changed_mask ==
		    GDK_WINDOW_STATE_FULLSCREEN)
			c->fullscreen = e->window_state.new_window_state &
			                GDK_WINDOW_STATE_FULLSCREEN;
		break;
	default:
		break;
	}

	return FALSE;
}

#ifdef WAYLAND_SUPPORT
#endif

void
showview(WebKitWebView *v, Client *c)
{
	GdkRGBA bgcolor = { 0 };
	GdkWindow *gwin;
	GtkCssProvider *css;
	char *cssstr;

	c->finder = webkit_web_view_get_find_controller(c->view);
	c->inspector = webkit_web_view_get_inspector(c->view);

	c->pageid = webkit_web_view_get_page_id(c->view);
	c->win = createwindow(c);

	/* Build layout: vbox with webview on top, status bar on bottom */
	c->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(c->vbox), GTK_WIDGET(c->view),
	                   TRUE, TRUE, 0);

	/* Status bar container */
	c->statusbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	/* Single entry widget - always visible, toggles between display/edit modes */
	c->statentry = gtk_entry_new();
	gtk_widget_set_hexpand(c->statentry, TRUE);
	gtk_widget_set_can_focus(c->statentry, FALSE); /* non-editable by default */
	gtk_editable_set_editable(GTK_EDITABLE(c->statentry), FALSE);
	gtk_entry_set_has_frame(GTK_ENTRY(c->statentry), FALSE);
	g_signal_connect(G_OBJECT(c->statentry), "activate",
	                 G_CALLBACK(baractivate), c);
	g_signal_connect(G_OBJECT(c->statentry), "key-press-event",
	                 G_CALLBACK(barkeypress), c);
	gtk_box_pack_start(GTK_BOX(c->statusbar), c->statentry, TRUE, TRUE, 0);

	gtk_box_pack_end(GTK_BOX(c->vbox), c->statusbar, FALSE, FALSE, 0);

	/* Style the status bar - thinner, pure black, single entry */
	css = gtk_css_provider_new();
	cssstr = g_strdup_printf(
	    "#surf-statusbar {"
	    "  background-color: %s;"
	    "  padding: 1px 6px;"
	    "  min-height: 18px;"
	    "}"
	    "#surf-statentry {"
	    "  background-color: %s;"
	    "  color: %s;"
	    "  font: %s;"
	    "  border: none;"
	    "  border-radius: 0;"
	    "  padding: 1px 6px;"
	    "  min-height: 18px;"
	    "}",
	    stat_bg_normal,
	    stat_bg_normal, stat_fg_normal, stat_font);
	gtk_css_provider_load_from_data(css, cssstr, -1, NULL);
	g_free(cssstr);

	gtk_widget_set_name(c->statusbar, "surf-statusbar");
	gtk_widget_set_name(c->statentry, "surf-statentry");

	gtk_style_context_add_provider(
	    gtk_widget_get_style_context(c->statusbar),
	    GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	gtk_style_context_add_provider(
	    gtk_widget_get_style_context(c->statentry),
	    GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(css);

	gtk_container_add(GTK_CONTAINER(c->win), c->vbox);
	gtk_widget_show_all(c->win);
	gtk_widget_grab_focus(GTK_WIDGET(c->view));

	gwin = gtk_widget_get_window(GTK_WIDGET(c->win));
#ifdef X11_SUPPORT
	if (display_ctx.backend == DISPLAY_BACKEND_X11) {
		c->xid = gdk_x11_window_get_xid(gwin);
	}
#endif
	generate_instance_id(c);
	updatewinid(c);
	if (showxidflag) {
		gdk_display_sync(gtk_widget_get_display(c->win));
		puts(winid);
		fflush(stdout);
	}

	if (curconfig[HideBackground].val.i)
		webkit_web_view_set_background_color(c->view, &bgcolor);

	if (!curconfig[KioskMode].val.i) {
		gdk_window_set_events(gwin, GDK_ALL_EVENTS_MASK);
#ifdef X11_SUPPORT
		if (display_ctx.backend == DISPLAY_BACKEND_X11) {
			gdk_window_add_filter(gwin, processx, c);
		}
#endif
	}

	if (curconfig[RunInFullscreen].val.i)
		togglefullscreen(c, NULL);

	if (curconfig[ZoomLevel].val.f != 1.0)
		webkit_web_view_set_zoom_level(c->view,
		                               curconfig[ZoomLevel].val.f);

	setatom(c, AtomFind, "");
	setatom(c, AtomUri, "about:blank");
	updatebar(c);
}

GtkWidget *
createwindow(Client *c)
{
	char *wmstr;
	GtkWidget *w;

	/* Always create toplevel window (no embedding) */
	w = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	wmstr = g_path_get_basename(argv0);
	gtk_window_set_wmclass(GTK_WINDOW(w), wmstr, "Surf");
	g_free(wmstr);

	wmstr = g_strdup_printf("%s[%"PRIu64"]", "Surf", c->pageid);
	gtk_window_set_role(GTK_WINDOW(w), wmstr);
	g_free(wmstr);

	gtk_window_set_default_size(GTK_WINDOW(w), winsize[0], winsize[1]);

	g_signal_connect(G_OBJECT(w), "destroy",
	                 G_CALLBACK(destroywin), c);
	g_signal_connect(G_OBJECT(w), "enter-notify-event",
	                 G_CALLBACK(winevent), c);
	g_signal_connect(G_OBJECT(w), "key-press-event",
	                 G_CALLBACK(winevent), c);
	g_signal_connect(G_OBJECT(w), "leave-notify-event",
	                 G_CALLBACK(winevent), c);
	g_signal_connect(G_OBJECT(w), "window-state-event",
	                 G_CALLBACK(winevent), c);

	return w;
}

gboolean
loadfailedtls(WebKitWebView *v, gchar *uri, GTlsCertificate *cert,
              GTlsCertificateFlags err, Client *c)
{
	GString *errmsg = g_string_new(NULL);
	gchar *html, *pem;

	c->failedcert = g_object_ref(cert);
	c->tlserr = err;
	c->errorpage = 1;

	if (err & G_TLS_CERTIFICATE_UNKNOWN_CA)
		g_string_append(errmsg,
		    "The signing certificate authority is not known.<br>");
	if (err & G_TLS_CERTIFICATE_BAD_IDENTITY)
		g_string_append(errmsg,
		    "The certificate does not match the expected identity "
		    "of the site that it was retrieved from.<br>");
	if (err & G_TLS_CERTIFICATE_NOT_ACTIVATED)
		g_string_append(errmsg,
		    "The certificate's activation time "
		    "is still in the future.<br>");
	if (err & G_TLS_CERTIFICATE_EXPIRED)
		g_string_append(errmsg, "The certificate has expired.<br>");
	if (err & G_TLS_CERTIFICATE_REVOKED)
		g_string_append(errmsg,
		    "The certificate has been revoked according to "
		    "the GTlsConnection's certificate revocation list.<br>");
	if (err & G_TLS_CERTIFICATE_INSECURE)
		g_string_append(errmsg,
		    "The certificate's algorithm is considered insecure.<br>");
	if (err & G_TLS_CERTIFICATE_GENERIC_ERROR)
		g_string_append(errmsg,
		    "Some error occurred validating the certificate.<br>");

	g_object_get(cert, "certificate-pem", &pem, NULL);
	html = g_strdup_printf("<p>Could not validate TLS for &ldquo;%s&rdquo;<br>%s</p>"
	                       "<p>You can inspect the following certificate "
	                       "with Ctrl-t (default keybinding).</p>"
	                       "<p><pre>%s</pre></p>", uri, errmsg->str, pem);
	g_free(pem);
	g_string_free(errmsg, TRUE);

	webkit_web_view_load_alternate_html(c->view, html, uri, NULL);
	g_free(html);

	return TRUE;
}

void loadchanged(WebKitWebView *v, WebKitLoadEvent e, Client *c) {
    const char *uri = geturi(c);

    switch (e) {
    case WEBKIT_LOAD_STARTED:
        setatom(c, AtomUri, uri);
        c->title = uri;
        c->https = c->insecure = 0;
        seturiparameters(c, uri, loadtransient);
        if (c->errorpage)
            c->errorpage = 0;
        else
            g_clear_object(&c->failedcert);
        break;
    case WEBKIT_LOAD_REDIRECTED:
        setatom(c, AtomUri, uri);
        c->title = uri;
        seturiparameters(c, uri, loadtransient);
        break;
    case WEBKIT_LOAD_COMMITTED:
        setatom(c, AtomUri, uri);
        c->title = uri;
        seturiparameters(c, uri, loadcommitted);
        c->https = webkit_web_view_get_tls_info(c->view, &c->cert,
                                                &c->tlserr);
        break;
    case WEBKIT_LOAD_FINISHED:
        seturiparameters(c, uri, loadfinished);
        /* Save to history with title */
        history_add(uri, c->title);
        runscript(c);
        break;
    }
    updatetitle(c);
}

void
progresschanged(WebKitWebView *v, GParamSpec *ps, Client *c)
{
	c->progress = webkit_web_view_get_estimated_load_progress(c->view) *
	              100;
	updatetitle(c);

#ifdef WAYLAND_SUPPORT
	/* Emit D-Bus signal for progress changes */
	if (display_ctx.backend == DISPLAY_BACKEND_WAYLAND) {
		/* Emit progress change signal if needed */
		/* For now, we focus on URI changes which are more important */
	}
#endif
}

void
titlechanged(WebKitWebView *view, GParamSpec *ps, Client *c)
{
	c->title = webkit_web_view_get_title(c->view);
	updatetitle(c);

	/* Update history entry with the actual title */
	const char *uri = geturi(c);
	if (c->title && *c->title && uri && !g_str_has_prefix(uri, "about:"))
		history_add(uri, c->title);

#ifdef WAYLAND_SUPPORT
	if (display_ctx.backend == DISPLAY_BACKEND_WAYLAND) {
	}
#endif
}

static gboolean
viewusrmsgrcv(WebKitWebView *v, WebKitUserMessage *m, gpointer unused)
{
	const char *name;
	GUnixFDList *gfd;
	WebKitUserMessage *r;
	Client *c;

	name = webkit_user_message_get_name(m);

	/* Find the client for this view */
	for (c = clients; c; c = c->next) {
		if (c->view == v)
			break;
	}
	if (!c)
		c = clients;
	if (!c)
		return TRUE;

	/* Handle hints data from extension */
	if (strcmp(name, "hints-data") == 0) {
		GVariant *params = webkit_user_message_get_parameters(m);
		if (params && g_variant_is_of_type(params, G_VARIANT_TYPE("a(siiii)"))) {
			hints_receive_data(c, params);
		}
		return TRUE;
	}

	if (strcmp(name, "page-created") != 0) {
		fprintf(stderr, "surf: Unknown UserMessage: %s\n", name);
		return TRUE;
	}

	/* Just acknowledge - no socket needed anymore */
	r = webkit_user_message_new("surf-ack", NULL);
	webkit_user_message_send_reply(m, r);

	return TRUE;
}

void
mousetargetchanged(WebKitWebView *v, WebKitHitTestResult *h, guint modifiers,
    Client *c)
{
	WebKitHitTestResultContext hc = webkit_hit_test_result_get_context(h);

	/* Keep the hit test to know where is the pointer on the next click */
	c->mousepos = h;

	if (hc & OnLink)
		c->targeturi = webkit_hit_test_result_get_link_uri(h);
	else if (hc & OnImg)
		c->targeturi = webkit_hit_test_result_get_image_uri(h);
	else if (hc & OnMedia)
		c->targeturi = webkit_hit_test_result_get_media_uri(h);
	else
		c->targeturi = NULL;

	c->overtitle = c->targeturi;
	updatetitle(c);
}

gboolean
permissionrequested(WebKitWebView *v, WebKitPermissionRequest *r, Client *c)
{
	ParamName param = ParameterLast;

	if (WEBKIT_IS_GEOLOCATION_PERMISSION_REQUEST(r)) {
		param = Geolocation;
	} else if (WEBKIT_IS_USER_MEDIA_PERMISSION_REQUEST(r)) {
		if (webkit_user_media_permission_is_for_audio_device(
		    WEBKIT_USER_MEDIA_PERMISSION_REQUEST(r)))
			param = AccessMicrophone;
		else if (webkit_user_media_permission_is_for_video_device(
		         WEBKIT_USER_MEDIA_PERMISSION_REQUEST(r)))
			param = AccessWebcam;
	} else {
		return FALSE;
	}

	if (curconfig[param].val.i)
		webkit_permission_request_allow(r);
	else
		webkit_permission_request_deny(r);

	return TRUE;
}

gboolean
decidepolicy(WebKitWebView *v, WebKitPolicyDecision *d,
    WebKitPolicyDecisionType dt, Client *c)
{
	switch (dt) {
	case WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION:
		decidenavigation(d, c);
		break;
	case WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION:
		decidenewwindow(d, c);
		break;
	case WEBKIT_POLICY_DECISION_TYPE_RESPONSE:
		decideresource(d, c);
		break;
	default:
		webkit_policy_decision_ignore(d);
		break;
	}
	return TRUE;
}

void
decidenavigation(WebKitPolicyDecision *d, Client *c)
{
	WebKitNavigationAction *a =
	    webkit_navigation_policy_decision_get_navigation_action(
	    WEBKIT_NAVIGATION_POLICY_DECISION(d));

	switch (webkit_navigation_action_get_navigation_type(a)) {
	case WEBKIT_NAVIGATION_TYPE_LINK_CLICKED: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_FORM_SUBMITTED: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_BACK_FORWARD: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_RELOAD: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_FORM_RESUBMITTED: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_OTHER: /* fallthrough */
	default:
		/* Do not navigate to links with a "_blank" target (popup) */
		if (webkit_navigation_action_get_frame_name(a)) {
			webkit_policy_decision_ignore(d);
		} else {
			/* Filter out navigation to different domain ? */
			/* get action→urirequest, copy and load in new window+view
			 * on Ctrl+Click ? */
			webkit_policy_decision_use(d);
		}
		break;
	}
}

void
decidenewwindow(WebKitPolicyDecision *d, Client *c)
{
	Arg arg;
	WebKitNavigationAction *a =
	    webkit_navigation_policy_decision_get_navigation_action(
	    WEBKIT_NAVIGATION_POLICY_DECISION(d));


	switch (webkit_navigation_action_get_navigation_type(a)) {
	case WEBKIT_NAVIGATION_TYPE_LINK_CLICKED: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_FORM_SUBMITTED: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_BACK_FORWARD: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_RELOAD: /* fallthrough */
	case WEBKIT_NAVIGATION_TYPE_FORM_RESUBMITTED:
		/* Filter domains here */
/* If the value of "mouse-button" is not 0, then the navigation was triggered by a mouse event.
 * test for link clicked but no button ? */
		arg.v = webkit_uri_request_get_uri(
		        webkit_navigation_action_get_request(a));
		newwindow(c, &arg, 0);
		break;
	case WEBKIT_NAVIGATION_TYPE_OTHER: /* fallthrough */
	default:
		break;
	}

	webkit_policy_decision_ignore(d);
}

void
decideresource(WebKitPolicyDecision *d, Client *c)
{
	int i, isascii = 1;
	WebKitResponsePolicyDecision *r = WEBKIT_RESPONSE_POLICY_DECISION(d);
	WebKitURIResponse *res =
	    webkit_response_policy_decision_get_response(r);
	const gchar *uri = webkit_uri_response_get_uri(res);

	if (g_str_has_suffix(uri, "/favicon.ico")) {
		webkit_policy_decision_ignore(d);
		return;
	}

	if (!g_str_has_prefix(uri, "http://")
	    && !g_str_has_prefix(uri, "https://")
	    && !g_str_has_prefix(uri, "about:")
	    && !g_str_has_prefix(uri, "file://")
	    && !g_str_has_prefix(uri, "webkit://")
	    && !g_str_has_prefix(uri, "data:")
	    && !g_str_has_prefix(uri, "blob:")
	    && !(g_str_has_prefix(uri, "webkit-pdfjs-viewer://") && curconfig[PDFJSviewer].val.i)
	    && strlen(uri) > 0) {
		for (i = 0; i < strlen(uri); i++) {
			if (!g_ascii_isprint(uri[i])) {
				isascii = 0;
				break;
			}
		}
		if (isascii) {
			handleplumb(c, uri);
			webkit_policy_decision_ignore(d);
			return;
		}
	}

	if (webkit_response_policy_decision_is_mime_type_supported(r)) {
		webkit_policy_decision_use(d);
	} else {
		webkit_policy_decision_ignore(d);
		download(c, res);
	}
}

void
insecurecontent(WebKitWebView *v, WebKitInsecureContentEvent e, Client *c)
{
	c->insecure = 1;
}

void
downloadstarted(WebKitWebContext *wc, WebKitDownload *d, Client *c)
{
	g_signal_connect(G_OBJECT(d), "notify::response",
	                 G_CALLBACK(responsereceived), c);
}

void
responsereceived(WebKitDownload *d, GParamSpec *ps, Client *c)
{
	download(c, webkit_download_get_response(d));
	webkit_download_cancel(d);
}

void
download(Client *c, WebKitURIResponse *r)
{
	Arg a = (Arg)DOWNLOAD(webkit_uri_response_get_uri(r), geturi(c));
	spawn(c, &a);
}

void
webprocessterminated(WebKitWebView *v, WebKitWebProcessTerminationReason r,
                     Client *c)
{
	fprintf(stderr, "web process terminated: %s\n",
	        r == WEBKIT_WEB_PROCESS_CRASHED ? "crashed" : "no memory");
	closeview(v, c);
}

void
closeview(WebKitWebView *v, Client *c)
{
	/* If we have tabs, close the tab instead of the window */
	if (tabs.count > 1) {
		/* Find which tab this view belongs to */
		for (int i = 0; i < tabs.count; i++) {
			if (tabs.views[i] == v) {
				/* Switch to this tab first if not active */
				if (i != tabs.active)
					tab_switch_to(c, i);
				tab_close(c, &(Arg){0});
				return;
			}
		}
	}
	gtk_widget_destroy(c->win);
}

void
destroywin(GtkWidget *w, Client *c)
{
	destroyclient(c);
	if (!clients)
		gtk_main_quit();
}


void
pasteuri(GtkClipboard *clipboard, const char *text, gpointer d)
{
	Arg a = {.v = text };
	if (text)
		loaduri((Client *) d, &a);
}

void
reload(Client *c, const Arg *a)
{
	if (a->i)
		webkit_web_view_reload_bypass_cache(c->view);
	else
		webkit_web_view_reload(c->view);
}

void
print(Client *c, const Arg *a)
{
	webkit_print_operation_run_dialog(webkit_print_operation_new(c->view),
	                                  GTK_WINDOW(c->win));
}

void
showcert(Client *c, const Arg *a)
{
	GTlsCertificate *cert = c->failedcert ? c->failedcert : c->cert;
	GcrCertificate *gcrt;
	GByteArray *crt;
	GtkWidget *win;
	GcrCertificateWidget *wcert;

	if (!cert)
		return;

	g_object_get(cert, "certificate", &crt, NULL);
	gcrt = gcr_simple_certificate_new(crt->data, crt->len);
	g_byte_array_unref(crt);

	win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	wcert = gcr_certificate_widget_new(gcrt);
	g_object_unref(gcrt);

	gtk_container_add(GTK_CONTAINER(win), GTK_WIDGET(wcert));
	gtk_widget_show_all(win);
}

void
clipboard(Client *c, const Arg *a)
{
	if (a->i) { /* load clipboard uri */
		gtk_clipboard_request_text(gtk_clipboard_get(
		                           GDK_SELECTION_PRIMARY),
		                           pasteuri, c);
	} else { /* copy uri */
		gtk_clipboard_set_text(gtk_clipboard_get(
		                       GDK_SELECTION_PRIMARY), c->targeturi
		                       ? c->targeturi : geturi(c), -1);
	}
}

void
zoom(Client *c, const Arg *a)
{
	if (a->i > 0)
		webkit_web_view_set_zoom_level(c->view,
		                               curconfig[ZoomLevel].val.f + 0.1);
	else if (a->i < 0)
		webkit_web_view_set_zoom_level(c->view,
		                               curconfig[ZoomLevel].val.f - 0.1);
	else
		webkit_web_view_set_zoom_level(c->view, 1.0);

	curconfig[ZoomLevel].val.f = webkit_web_view_get_zoom_level(c->view);
}

static void
msgext(Client *c, char type, const Arg *a)
{
	char js[128];

	switch (type) {
	case 'v':
		snprintf(js, sizeof(js),
		         "window.scrollBy(0,window.innerHeight/100*%d);",
		         a->i);
		break;
	case 'h':
		snprintf(js, sizeof(js),
		         "window.scrollBy(window.innerWidth/100*%d,0);",
		         a->i);
		break;
	default:
		fprintf(stderr, "surf: msgext unknown type: %c\n", type);
		return;
	}

	webkit_web_view_evaluate_javascript(c->view, js, -1,
	    NULL, NULL, NULL, NULL, NULL);
}

void
scrollv(Client *c, const Arg *a)
{
	msgext(c, 'v', a);
}

void
scrollh(Client *c, const Arg *a)
{
	msgext(c, 'h', a);
}

void
navigate(Client *c, const Arg *a)
{
	if (a->i < 0)
		webkit_web_view_go_back(c->view);
	else if (a->i > 0)
		webkit_web_view_go_forward(c->view);
}

void
stop(Client *c, const Arg *a)
{
	webkit_web_view_stop_loading(c->view);
}

void
toggle(Client *c, const Arg *a)
{
	curconfig[a->i].val.i ^= 1;
	setparameter(c, 1, (ParamName)a->i, &curconfig[a->i].val);
}

void
togglefullscreen(Client *c, const Arg *a)
{
	/* toggling value is handled in winevent() */
	if (c->fullscreen)
		gtk_window_unfullscreen(GTK_WINDOW(c->win));
	else
		gtk_window_fullscreen(GTK_WINDOW(c->win));
}

void
togglecookiepolicy(Client *c, const Arg *a)
{
	++cookiepolicy;
	cookiepolicy %= strlen(curconfig[CookiePolicies].val.v);

	setparameter(c, 0, CookiePolicies, NULL);
}

void
toggleinspector(Client *c, const Arg *a)
{
	if (webkit_web_inspector_is_attached(c->inspector))
		webkit_web_inspector_close(c->inspector);
	else if (curconfig[Inspector].val.i)
		webkit_web_inspector_show(c->inspector);
}

void
find(Client *c, const Arg *a)
{
	const char *s, *f;

	if (a && a->i) {
		if (a->i > 0)
			webkit_find_controller_search_next(c->finder);
		else
			webkit_find_controller_search_previous(c->finder);
	} else {
		s = getatom(c, AtomFind);
		f = webkit_find_controller_get_search_text(c->finder);

		if (g_strcmp0(f, s) == 0) /* reset search */
			webkit_find_controller_search(c->finder, "", findopts,
			                              G_MAXUINT);

		webkit_find_controller_search(c->finder, s, findopts,
		                              G_MAXUINT);

		if (strcmp(s, "") == 0)
			webkit_find_controller_search_finish(c->finder);
	}
}

void
updatebar(Client *c)
{
	char *text;
	const char *uri, *modestr;

	if (!c->statentry)
		return;

	/* Don't update if in command mode (user is editing) */
	if (c->mode == ModeCommand)
		return;

	uri = geturi(c);

	switch (c->mode) {
	case ModeInsert:  modestr = "INSERT"; break;
	case ModeHint:    modestr = "HINT"; break;
	case ModeCommand: modestr = "COMMAND"; break;
	default:          modestr = "NORMAL"; break;
	}

	if (c->progress > 0 && c->progress < 100)
		text = g_strdup_printf(" [%s] [%i%%] %s", modestr,
		                       c->progress, uri);
	else
		text = g_strdup_printf(" [%s] %s", modestr, uri);

	gtk_entry_set_text(GTK_ENTRY(c->statentry), text);
	g_free(text);
}

void
openbar(Client *c, const Arg *a)
{
	const char *uri;

	c->mode = ModeCommand;

	/* Load history for completions */
	history_load();

	/* Make entry editable and focusable */
	gtk_widget_set_can_focus(c->statentry, TRUE);
	gtk_editable_set_editable(GTK_EDITABLE(c->statentry), TRUE);

	if (a->i) {
		/* 'e' - prefill with current URL */
		uri = geturi(c);
		gtk_entry_set_text(GTK_ENTRY(c->statentry),
		                   g_strdup_printf(" [COMMAND] %s", uri));
	} else {
		/* 'o' - empty for new URL */
		gtk_entry_set_text(GTK_ENTRY(c->statentry), " [COMMAND] ");
	}

	gtk_widget_grab_focus(c->statentry);
	gtk_editable_set_position(GTK_EDITABLE(c->statentry), -1);

	history_filter(c, "");

	updatetitle(c);
}

void
closebar(Client *c)
{
	c->mode = ModeNormal;

	history_hide(c);

	/* Make entry non-editable and non-focusable */
	gtk_editable_set_editable(GTK_EDITABLE(c->statentry), FALSE);
	gtk_widget_set_can_focus(c->statentry, FALSE);

	gtk_widget_grab_focus(GTK_WIDGET(c->view));
	updatetitle(c);
	updatebar(c);
}

void
baractivate(GtkEntry *entry, Client *c)
{
	const char *text, *url;
	Arg a;

	text = gtk_entry_get_text(entry);

	/* Skip past "[COMMAND] " prefix */
	url = text;
	if (g_str_has_prefix(text, " [COMMAND] "))
		url = text + 11;

	history_hide(c);

	if (url && *url) {
		a.v = url;
		loaduri(c, &a);
	}

	closebar(c);
}

gboolean
barkeypress(GtkWidget *w, GdkEvent *e, Client *c)
{
	if (e->key.keyval == GDK_KEY_Escape) {
		closebar(c);
		return TRUE;
	}

	/* Tab / Shift+Tab to navigate completions */
	if (e->key.keyval == GDK_KEY_Tab) {
		if (e->key.state & GDK_SHIFT_MASK)
			history_select(c, -1);
		else
			history_select(c, +1);
		return TRUE;
	}

	/* Ctrl+n / Ctrl+p for completion navigation */
	if ((e->key.state & GDK_CONTROL_MASK) && e->key.keyval == GDK_KEY_n) {
		history_select(c, +1);
		return TRUE;
	}
	if ((e->key.state & GDK_CONTROL_MASK) && e->key.keyval == GDK_KEY_p) {
		history_select(c, -1);
		return TRUE;
	}

	/* After any other key, update filter on idle so the entry text is updated first */
	g_idle_add((GSourceFunc)bar_update_filter, c);

	return FALSE;
}

static gboolean
bar_update_filter(gpointer data)
{
	Client *c = (Client *)data;
	const char *text;

	if (c->mode != ModeCommand)
		return FALSE;

	text = gtk_entry_get_text(GTK_ENTRY(c->statentry));

	/* Skip past "[COMMAND] " prefix */
	if (g_str_has_prefix(text, " [COMMAND] "))
		text = text + 11;

	history_filter(c, text);

	return FALSE; /* don't repeat */
}

void
toggleinsert(Client *c, const Arg *a)
{
	if (c->mode == ModeInsert)
		c->mode = ModeNormal;
	else
		c->mode = ModeInsert;
	updatetitle(c);
}

void
clicknavigate(Client *c, const Arg *a, WebKitHitTestResult *h)
{
	navigate(c, a);
}

void
clicknewwindow(Client *c, const Arg *a, WebKitHitTestResult *h)
{
	Arg arg;

	arg.v = webkit_hit_test_result_get_link_uri(h);
	newwindow(c, &arg, a->i);
}

void
clickexternplayer(Client *c, const Arg *a, WebKitHitTestResult *h)
{
	Arg arg;

	arg = (Arg)VIDEOPLAY(webkit_hit_test_result_get_media_uri(h));
	spawn(c, &arg);
}

static gboolean
fifo_read(GIOChannel *chan, GIOCondition cond, gpointer data)
{
    Client *c;
    gchar *line = NULL;
    gsize len;
    GError *err = NULL;

    /* Use the first client (active window) */
    c = clients;
    if (!c)
        return TRUE;

    if (g_io_channel_read_line(chan, &line, &len, NULL, &err) == G_IO_STATUS_NORMAL) {
        if (line) {
            g_strstrip(line);
            fprintf(stderr, "fifo cmd: %s\n", line);

            if (g_str_has_prefix(line, "message-error ")) {
                fprintf(stderr, "userscript error: %s\n", line + 14);
            } else if (g_str_has_prefix(line, "message-info ")) {
                fprintf(stderr, "userscript info: %s\n", line + 13);
            } else if (g_str_has_prefix(line, "jseval ")) {
                const char *js = line + 7;
                while (*js == '-') {
                    while (*js && *js != ' ')
                        js++;
                    while (*js == ' ')
                        js++;
                }
                if (*js)
                    evalscript(c, "%s", js);
            } else if (g_str_has_prefix(line, "open ")) {
                const char *url = line + 5;
                gboolean new_tab = FALSE;
                while (*url == '-') {
                    if (g_str_has_prefix(url, "-t")) {
                        new_tab = TRUE;
                    }
                    while (*url && *url != ' ')
                        url++;
                    while (*url == ' ')
                        url++;
                }
                if (*url) {
                    Arg a = { .v = url };
                    if (new_tab)
                        newwindow(c, &a, 0);
                    else
                        loaduri(c, &a);
                }
            }
            g_free(line);
        }
    }

    if (err) g_error_free(err);
    return TRUE;
}

void
spawnuserscript(Client *c, const Arg *a)
{
    const char *script = a->v;
    const char *uri = geturi(c);
    const char *title = c->title ? c->title : "";

    if (!surf_fifo) {
        fprintf(stderr, "spawnuserscript: no fifo\n");
        return;
    }

    /* Fork and exec the userscript with environment set up */
    if (fork() == 0) {
        /* Set environment variables compatible with qutebrowser userscripts */
        setenv("SURF_FIFO", surf_fifo, 1);
        setenv("SURF_URL", uri, 1);
        setenv("SURF_TITLE", title, 1);
        setenv("SURF_MODE", c->mode == ModeInsert ? "insert" : "normal", 1);

        /* qutebrowser compatibility aliases */
        setenv("QUTE_FIFO", surf_fifo, 1);
        setenv("QUTE_URL", uri, 1);
        setenv("QUTE_TITLE", title, 1);
        setenv("QUTE_MODE", c->mode == ModeInsert ? "insert" : "normal", 1);

#ifdef X11_SUPPORT
        if (display_ctx.backend == DISPLAY_BACKEND_X11 && display_ctx.data.x11.dpy)
            close(ConnectionNumber(display_ctx.data.x11.dpy));
#endif
        close(spair[0]);
        close(spair[1]);
        setsid();
        execl("/bin/sh", "sh", "-c", script, NULL);
        fprintf(stderr, "spawnuserscript: exec failed: %s\n", script);
        exit(1);
    }
}

static gchar *
preprocess_userscript(const gchar *script)
{
    /* Check if this script needs main-world access */
    gboolean needs_main_world = (strstr(script, "unsafeWindow") != NULL ||
                                  strstr(script, "@grant unsafeWindow") != NULL ||
                                  strstr(script, "@grant none") != NULL ||
                                  strstr(script, "window.fetch") != NULL ||
                                  strstr(script, "w.fetch") != NULL);

    gboolean is_doc_start = (strstr(script, "@run-at") != NULL &&
                              strstr(script, "document-start") != NULL);

    if (needs_main_world) {
        GString *out = g_string_new(NULL);

        /* NO script element - just shims + script directly */
        /* evalscript bypasses CSP since it's native injection */
        g_string_append(out,
            "(function(){\n"
            "var unsafeWindow = window;\n"
            "var GM_info = {script:{name:'userscript',version:'1.0'},scriptHandler:'Surf'};\n"
            "function GM_getValue(k,d){try{var v=localStorage.getItem('_gm_'+k);return v!==null?JSON.parse(v):d;}catch(e){return d;}}\n"
            "function GM_setValue(k,v){try{localStorage.setItem('_gm_'+k,JSON.stringify(v));}catch(e){}}\n"
            "function GM_deleteValue(k){try{localStorage.removeItem('_gm_'+k);}catch(e){}}\n"
            "function GM_addStyle(c){var s=document.createElement('style');s.textContent=c;(document.head||document.documentElement).appendChild(s);}\n"
            "function GM_xmlhttpRequest(d){var x=new XMLHttpRequest();x.open(d.method||'GET',d.url,true);x.onload=function(){if(d.onload)d.onload({responseText:x.responseText,status:x.status});};x.send(d.data||null);}\n"
            "var GM={getValue:function(k,d){return Promise.resolve(GM_getValue(k,d));},setValue:function(k,v){GM_setValue(k,v);return Promise.resolve();}};\n"
        );

        g_string_append(out, script);
        g_string_append(out, "\n})();\n");

        return g_string_free(out, FALSE);
    }

    /* Non-main-world scripts: wrap in IIFE with GM shims directly */
    GString *out = g_string_new(NULL);

    g_string_append(out,
        "(function() {\n"
        "'use strict';\n"
        "\n"
        "var unsafeWindow = window;\n"
        "var GM_info = {\n"
        "  script: { name: 'userscript', version: '1.0', description: '' },\n"
        "  scriptHandler: 'Surf'\n"
        "};\n"
        "function GM_getValue(k,d){try{var v=localStorage.getItem('_gm_'+k);if(v===null)return d;try{return JSON.parse(v);}catch(e){return v;}}catch(e){return d;}}\n"
        "function GM_setValue(k,v){try{localStorage.setItem('_gm_'+k,JSON.stringify(v));}catch(e){}}\n"
        "function GM_deleteValue(k){try{localStorage.removeItem('_gm_'+k);}catch(e){}}\n"
        "function GM_listValues(){var r=[];try{for(var i=0;i<localStorage.length;i++){var k=localStorage.key(i);if(k.indexOf('_gm_')===0)r.push(k.substring(4));}}catch(e){}return r;}\n"
        "function GM_log(){console.log.apply(console,['[GM]'].concat(Array.prototype.slice.call(arguments)));}\n"
        "function GM_openInTab(u){return window.open(u,'_blank');}\n"
        "function GM_addStyle(c){var s=document.createElement('style');s.textContent=c;(document.head||document.documentElement).appendChild(s);return s;}\n"
        "function GM_setClipboard(t){if(navigator.clipboard&&navigator.clipboard.writeText)navigator.clipboard.writeText(t);}\n"
        "function GM_xmlhttpRequest(d){var x=new XMLHttpRequest();x.open(d.method||'GET',d.url,true);if(d.headers)Object.keys(d.headers).forEach(function(k){try{x.setRequestHeader(k,d.headers[k]);}catch(e){}});if(d.responseType)x.responseType=d.responseType;x.onload=function(){var r={responseText:x.responseText,response:x.response,status:x.status,statusText:x.statusText,readyState:x.readyState,finalUrl:x.responseURL,responseHeaders:x.getAllResponseHeaders()};if(d.onload)d.onload(r);};x.onerror=function(){if(d.onerror)d.onerror({status:x.status});};x.send(d.data||null);return{abort:function(){x.abort();}};};\n"
        "var GM={getValue:function(k,d){return Promise.resolve(GM_getValue(k,d));},setValue:function(k,v){GM_setValue(k,v);return Promise.resolve();},deleteValue:function(k){GM_deleteValue(k);return Promise.resolve();},listValues:function(){return Promise.resolve(GM_listValues());},openInTab:function(u){return Promise.resolve(GM_openInTab(u));},setClipboard:function(t){GM_setClipboard(t);return Promise.resolve();},xmlHttpRequest:GM_xmlhttpRequest,info:GM_info};\n"
        "function GM_registerMenuCommand(){}\n"
        "function GM_unregisterMenuCommand(){}\n"
        "function GM_getResourceText(){return '';}\n"
        "function GM_getResourceURL(){return '';}\n"
        "function GM_notification(d){if(typeof d==='string')d={text:d};if(window.Notification&&Notification.permission==='granted')new Notification(d.title||'Userscript',{body:d.text});}\n"
        "\n"
    );

    /* Parse script metadata for GM_info */
    const char *meta_start = strstr(script, "// ==UserScript==");
    const char *meta_end = strstr(script, "// ==/UserScript==");
    if (meta_start && meta_end) {
        const char *name_tag = strstr(meta_start, "// @name");
        if (name_tag && name_tag < meta_end) {
            name_tag += 8;
            while (*name_tag == ' ' || *name_tag == '\t') name_tag++;
            const char *name_end = strchr(name_tag, '\n');
            if (name_end) {
                gchar *name = g_strndup(name_tag, name_end - name_tag);
                g_strstrip(name);
                gchar *escaped = g_strescape(name, NULL);
                g_string_append_printf(out,
                    "GM_info.script.name = \"%s\";\n", escaped);
                g_free(escaped);
                g_free(name);
            }
        }

        const char *ver_tag = strstr(meta_start, "// @version");
        if (ver_tag && ver_tag < meta_end) {
            ver_tag += 11;
            while (*ver_tag == ' ' || *ver_tag == '\t') ver_tag++;
            const char *ver_end = strchr(ver_tag, '\n');
            if (ver_end) {
                gchar *ver = g_strndup(ver_tag, ver_end - ver_tag);
                g_strstrip(ver);
                gchar *escaped = g_strescape(ver, NULL);
                g_string_append_printf(out,
                    "GM_info.script.version = \"%s\";\n", escaped);
                g_free(escaped);
                g_free(ver);
            }
        }
    }

    g_string_append(out, "\n/* --- User Script --- */\n");
    g_string_append(out, script);
    g_string_append(out, "\n})();\n");

    return g_string_free(out, FALSE);
}

static void
setup_fifo(Client *c)
{
    int fd;

    /* Only create once */
    if (surf_fifo)
        return;

    surf_fifo = g_strdup_printf("/tmp/surf-fifo-%ld", (long)getpid());

    /* Remove stale fifo if it exists */
    unlink(surf_fifo);
    if (mkfifo(surf_fifo, 0600) != 0) {
        fprintf(stderr, "failed to create fifo: %s\n", surf_fifo);
        g_free(surf_fifo);
        surf_fifo = NULL;
        return;
    }

    /* open nonblocking so we don't hang */
    fd = open(surf_fifo, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "failed to open fifo: %s\n", surf_fifo);
        g_free(surf_fifo);
        surf_fifo = NULL;
        return;
    }

    fifo_chan = g_io_channel_unix_new(fd);
    g_io_channel_set_encoding(fifo_chan, NULL, NULL);
    g_io_channel_set_flags(fifo_chan,
        g_io_channel_get_flags(fifo_chan) | G_IO_FLAG_NONBLOCK, NULL);
    g_io_channel_set_close_on_unref(fifo_chan, TRUE);
    g_io_add_watch(fifo_chan, G_IO_IN, fifo_read, c);

    fprintf(stderr, "fifo created: %s\n", surf_fifo);
}

static void
inject_userscripts_early(WebKitUserContentManager *cm, const char *uri)
{
    char *scriptdir = g_build_filename(g_get_home_dir(), ".surf", "userscripts", NULL);
    fprintf(stderr, "inject_userscripts_early: looking in %s\n", scriptdir);
    GDir *dir = g_dir_open(scriptdir, 0, NULL);
    const char *filename;

    if (!dir) {
        fprintf(stderr, "inject_userscripts_early: cant open dir %s\n", scriptdir);
        g_free(scriptdir);
        return;
    }

    while ((filename = g_dir_read_name(dir))) {
        if (!g_str_has_suffix(filename, ".user.js")) {
            fprintf(stderr, "inject_userscripts_early: skipping non-userscript: %s\n", filename);
            continue;
        }

        char *filepath = g_build_filename(scriptdir, filename, NULL);
        gchar *script = NULL;
        gsize len;

        if (!g_file_get_contents(filepath, &script, &len, NULL) || !len) {
            fprintf(stderr, "inject_userscripts_early: failed to read %s\n", filename);
            g_free(filepath);
            continue;
        }

        /* Determine injection time from metadata */
        gboolean is_doc_start = (strstr(script, "@run-at") != NULL &&
                                 strstr(script, "document-start") != NULL);

        /* Check if script needs page-world access */
        gboolean needs_page_world = (strstr(script, "unsafeWindow") != NULL ||
                                     strstr(script, "@grant none") != NULL ||
                                     strstr(script, "@grant unsafeWindow") != NULL);

        /* Extract @match, @include, @exclude patterns from metadata */
        GPtrArray *allow = g_ptr_array_new_with_free_func(g_free);
        GPtrArray *exclude = g_ptr_array_new_with_free_func(g_free);
        gboolean has_patterns = FALSE;

        char **lines = g_strsplit(script, "\n", -1);
        for (int i = 0; lines[i]; i++) {
            char *line = g_strstrip(lines[i]);
            if (g_str_has_prefix(line, "// ==/UserScript=="))
                break;
            if (g_str_has_prefix(line, "// @match ")) {
                char *pattern = g_strstrip(g_strdup(line + 10));
                g_ptr_array_add(allow, pattern);
                has_patterns = TRUE;
            } else if (g_str_has_prefix(line, "// @include ")) {
                char *pattern = g_strstrip(g_strdup(line + 12));
                g_ptr_array_add(allow, pattern);
                has_patterns = TRUE;
            } else if (g_str_has_prefix(line, "// @exclude ")) {
                char *pattern = g_strstrip(g_strdup(line + 12));
                g_ptr_array_add(exclude, pattern);
            }
        }
        g_strfreev(lines);

        /* NULL-terminate arrays for WebKit API */
        g_ptr_array_add(allow, NULL);
        g_ptr_array_add(exclude, NULL);

        gchar *processed = preprocess_userscript(script);
        fprintf(stderr, "inject_userscripts_early: processed preview: %.200s\n", processed);

        WebKitUserScriptInjectionTime inject_time = is_doc_start ?
            WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START :
            WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END;

        const char * const *allow_list = has_patterns ?
            (const char * const *)allow->pdata : NULL;
        const char * const *exclude_list = has_patterns ?
            (const char * const *)exclude->pdata : NULL;

        WebKitUserScript *us;

        if (needs_page_world) {
            /*
             * Scripts with @grant none or unsafeWindow need access to the
             * page's real window object. We inject into a named world that
             * shares the page's DOM and JS context.
             *
             * WEBKIT_USER_CONTENT_INJECT_TOP_FRAME avoids running in
             * iframes where it could break things.
             */
            us = webkit_user_script_new_for_world(
                processed,
                WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
                inject_time,
                "surf-page-world",
                allow_list,
                exclude_list);
        } else {
            /*
             * Normal scripts run in WebKit's isolated world.
             * They get their own JS context but can still access the DOM.
             * WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES lets them run in
             * iframes too (matching Greasemonkey behavior).
             */
            us = webkit_user_script_new(
                processed,
                WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
                inject_time,
                allow_list,
                exclude_list);
        }

        webkit_user_content_manager_add_script(cm, us);
        webkit_user_script_unref(us);
        g_free(processed);

        fprintf(stderr, "inject_userscripts_early: registered (%s%s): %s\n",
                is_doc_start ? "document-start" : "document-end",
                needs_page_world ? ", page-world" : "",
                filename);

        g_ptr_array_free(allow, TRUE);
        g_ptr_array_free(exclude, TRUE);
        g_free(script);
        g_free(filepath);
    }

    g_dir_close(dir);
    g_free(scriptdir);
}

static void
history_load(void)
{
	gchar *contents = NULL;
	gchar **lines;
	int i;

	if (history_entries) {
		for (i = 0; i < (int)history_entries->len; i++) {
			HistoryEntry *e = &g_array_index(history_entries, HistoryEntry, i);
			g_free(e->uri);
			g_free(e->title);
		}
		g_array_free(history_entries, TRUE);
	}

	history_entries = g_array_new(FALSE, TRUE, sizeof(HistoryEntry));

	if (!historyfile)
		return;

	if (!g_file_get_contents(historyfile, &contents, NULL, NULL) || !contents)
		return;

	lines = g_strsplit(contents, "\n", -1);
	for (i = 0; lines[i]; i++) {
		if (!lines[i][0])
			continue;

		/* Format: timestamp<space>url[<space>title] */
		char *space1 = strchr(lines[i], ' ');
		if (!space1)
			continue;

		long ts = atol(lines[i]);
		char *url = space1 + 1;
		char *space2 = strchr(url, ' ');
		
		HistoryEntry entry;
		entry.timestamp = ts;
		if (space2) {
			entry.uri = g_strndup(url, space2 - url);
			entry.title = g_strdup(space2 + 1);
		} else {
			entry.uri = g_strdup(url);
			entry.title = NULL;
		}
		g_array_append_val(history_entries, entry);
	}

	g_strfreev(lines);
	g_free(contents);
}

static void
history_add(const char *uri, const char *title)
{
	FILE *f;
	gchar *contents = NULL;

	if (!uri || !*uri || !historyfile)
		return;
	if (g_str_has_prefix(uri, "about:"))
		return;
	if (g_str_has_prefix(uri, "file://"))
		return;

	/* Read existing history */
	gboolean have_contents = g_file_get_contents(historyfile, &contents, NULL, NULL) && contents;

	if (have_contents) {
		gchar **lines = g_strsplit(contents, "\n", -1);
		GString *newcontents = g_string_new(NULL);
		gboolean dominated = FALSE;

		/* Check if the most recent entry already has this URL WITH a title */
		for (int i = g_strv_length(lines) - 1; i >= 0; i--) {
			if (!lines[i] || !*lines[i])
				continue;

			char *space = strchr(lines[i], ' ');
			if (!space)
				break;

			char *saved_url = space + 1;
			char *next_space = strchr(saved_url, ' ');
			gchar *url_only;
			if (next_space)
				url_only = g_strndup(saved_url, next_space - saved_url);
			else
				url_only = g_strdup(saved_url);

			if (strcmp(url_only, uri) == 0) {
				/* Same URL exists - only skip if it already has a title
				 * and we don't have a better one */
				if (next_space && *(next_space + 1) && (!title || !*title)) {
					g_free(url_only);
					g_strfreev(lines);
					g_free(contents);
					return; /* Already have a titled entry, nothing new to add */
				}
			}
			g_free(url_only);
			break; /* Only check most recent */
		}

		/* Remove ALL old entries with same URI */
		for (int i = 0; lines[i]; i++) {
			if (!lines[i][0])
				continue;
			char *space = strchr(lines[i], ' ');
			if (!space)
				continue;
			char *saved_url = space + 1;
			char *next_space = strchr(saved_url, ' ');
			gchar *url_only;
			if (next_space)
				url_only = g_strndup(saved_url, next_space - saved_url);
			else
				url_only = g_strdup(saved_url);

			if (strcmp(url_only, uri) != 0) {
				g_string_append(newcontents, lines[i]);
				g_string_append_c(newcontents, '\n');
			}
			g_free(url_only);
		}

		g_strfreev(lines);
		g_free(contents);

		if (!(f = fopen(historyfile, "w"))) {
			g_string_free(newcontents, TRUE);
			return;
		}
		fwrite(newcontents->str, 1, newcontents->len, f);
		g_string_free(newcontents, TRUE);
	} else {
		if (!(f = fopen(historyfile, "a")))
			return;
	}

	if (title && *title)
		fprintf(f, "%ld %s %s\n", (long)time(NULL), uri, title);
	else
		fprintf(f, "%ld %s\n", (long)time(NULL), uri);
	fclose(f);
}

static void
history_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer data)
{
	Client *c = (Client *)data;
	GtkWidget *label;
	const char *text;

	label = gtk_bin_get_child(GTK_BIN(row));
	if (!label)
		return;

	text = gtk_widget_get_name(label);
	if (text && *text) {
		Arg a = { .v = text };
		history_hide(c);
		closebar(c);
		loaduri(c, &a);
	}
}

static void
history_hide(Client *c)
{
	if (history_scroll) {
		gtk_widget_destroy(history_scroll);
		history_scroll = NULL;
		history_list = NULL;
	}
	history_selected = -1;
}


static void
history_filter(Client *c, const char *text)
{
	int count = 0;
	const int max_results = 15;
	GHashTable *seen;
	GList *children, *iter;
	GtkCssProvider *css;

	if (!history_entries || !text)
		return;

	if (g_str_has_prefix(text, " [COMMAND] "))
		text = text + 11;

	if (!history_scroll) {
		history_scroll = gtk_scrolled_window_new(NULL, NULL);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(history_scroll),
			GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

		history_list = gtk_list_box_new();
		gtk_list_box_set_selection_mode(GTK_LIST_BOX(history_list),
			GTK_SELECTION_NONE);
		gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(history_list), TRUE);
		g_signal_connect(history_list, "row-activated",
			G_CALLBACK(history_row_activated), c);
		gtk_container_add(GTK_CONTAINER(history_scroll), history_list);

		/* Find where statusbar is */
		GList *kids = gtk_container_get_children(GTK_CONTAINER(c->vbox));
		int statusbar_pos = 0;
		int idx = 0;
		for (GList *l = kids; l; l = l->next, idx++) {
			if (l->data == c->statusbar) {
				statusbar_pos = idx;
				break;
			}
		}
		g_list_free(kids);

		/* Insert history right before statusbar */
		gtk_box_pack_start(GTK_BOX(c->vbox), history_scroll, FALSE, FALSE, 0);
		gtk_box_reorder_child(GTK_BOX(c->vbox), history_scroll, statusbar_pos);

		css = gtk_css_provider_new();
		gtk_css_provider_load_from_data(css,
			"#history-scroll {"
			"  background-color: #1a1a1a;"
			"}"
			"#history-list {"
			"  background-color: #1a1a1a;"
			"}"
			"#history-list row {"
			"  background-color: #1a1a1a;"
			"  color: #cccccc;"
			"  padding: 0;"
			"  outline: none;"
			"  border: none;"
			"  box-shadow: none;"
			"}"
			"#history-list row:focus {"
			"  background-color: #1a1a1a;"
			"  outline: none;"
			"  box-shadow: none;"
			"}"
			"#history-list row:hover {"
			"  background-color: #1a1a1a;"
			"}"
			"#history-list row.hl {"
			"  background-color: #333333;"
			"}"
			"#history-list row label {"
			"  font-family: monospace;"
			"  font-size: 11px;"
			"  padding: 2px 6px;"
			"  color: #cccccc;"
			"}", -1, NULL);

		gtk_widget_set_name(history_scroll, "history-scroll");
		gtk_widget_set_name(history_list, "history-list");

		GtkStyleContext *sc;
		sc = gtk_widget_get_style_context(history_scroll);
		gtk_style_context_add_provider(sc, GTK_STYLE_PROVIDER(css),
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
		sc = gtk_widget_get_style_context(history_list);
		gtk_style_context_add_provider(sc, GTK_STYLE_PROVIDER(css),
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);

		g_object_set_data_full(G_OBJECT(history_list), "css",
			css, g_object_unref);
	}

	/* Clear existing rows */
	children = gtk_container_get_children(GTK_CONTAINER(history_list));
	for (iter = children; iter; iter = iter->next)
		gtk_widget_destroy(GTK_WIDGET(iter->data));
	g_list_free(children);

	seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

	for (int i = history_entries->len - 1; i >= 0 && count < max_results; i--) {
		HistoryEntry *e = &g_array_index(history_entries, HistoryEntry, i);

		if (g_hash_table_contains(seen, e->uri))
			continue;

		if (text[0]) {
			gchar *uri_lower = g_utf8_strdown(e->uri, -1);
			gchar *text_lower = g_utf8_strdown(text, -1);
			gchar *title_lower = e->title ? g_utf8_strdown(e->title, -1) : NULL;

			gboolean match = (strstr(uri_lower, text_lower) != NULL);
			if (!match && title_lower)
				match = (strstr(title_lower, text_lower) != NULL);

			if (!match) {
				gchar **words = g_strsplit(text_lower, " ", -1);
				match = TRUE;
				for (int w = 0; words[w]; w++) {
					if (!words[w][0])
						continue;
					gboolean word_match = (strstr(uri_lower, words[w]) != NULL);
					if (!word_match && title_lower)
						word_match = (strstr(title_lower, words[w]) != NULL);
					if (!word_match) {
						match = FALSE;
						break;
					}
				}
				g_strfreev(words);
			}

			g_free(uri_lower);
			g_free(text_lower);
			g_free(title_lower);

			if (!match)
				continue;
		}

		g_hash_table_add(seen, g_strdup(e->uri));

		gchar *uri_escaped = g_markup_escape_text(e->uri, -1);
		gchar *display_text;
		if (e->title && *e->title) {
			gchar *title_escaped = g_markup_escape_text(e->title, -1);
			display_text = g_strdup_printf(
				"<span foreground='#87afd7'>%s</span>  <span foreground='#666666'>%s</span>",
				uri_escaped, title_escaped);
			g_free(title_escaped);
		} else {
			display_text = g_strdup_printf(
				"<span foreground='#87afd7'>%s</span>", uri_escaped);
		}
		g_free(uri_escaped);

		GtkWidget *label = gtk_label_new(NULL);
		gtk_label_set_markup(GTK_LABEL(label), display_text);
		gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
		gtk_label_set_xalign(GTK_LABEL(label), 0.0);
		gtk_widget_set_name(label, e->uri);
		g_free(display_text);

		gtk_list_box_insert(GTK_LIST_BOX(history_list), label, -1);

		/* Apply CSS to each new row */
		GtkListBoxRow *row = gtk_list_box_get_row_at_index(
			GTK_LIST_BOX(history_list), count);
		if (row) {
			GtkCssProvider *row_css = g_object_get_data(
				G_OBJECT(history_list), "css");
			if (row_css) {
				gtk_style_context_add_provider(
					gtk_widget_get_style_context(GTK_WIDGET(row)),
					GTK_STYLE_PROVIDER(row_css),
					GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
			}
		}

		count++;
	}

	g_hash_table_destroy(seen);
	history_selected = -1;

	if (count > 0) {
		int row_height = 24;
		int popup_height = count * row_height;
		if (popup_height > 360)
			popup_height = 360;
		gtk_widget_set_size_request(history_scroll, -1, popup_height);
		gtk_widget_show_all(history_scroll);
	} else if (history_scroll) {
		gtk_widget_hide(history_scroll);
	}
}

static void
history_select(Client *c, int direction)
{
	GtkListBoxRow *row;
	GtkCssProvider *row_css;
	int n;

	if (!history_list)
		return;

	row_css = g_object_get_data(G_OBJECT(history_list), "css");

	n = 0;
	while (gtk_list_box_get_row_at_index(GTK_LIST_BOX(history_list), n))
		n++;

	if (n == 0)
		return;

	/* Remove old highlight */
	if (history_selected >= 0) {
		row = gtk_list_box_get_row_at_index(
			GTK_LIST_BOX(history_list), history_selected);
		if (row)
			gtk_style_context_remove_class(
				gtk_widget_get_style_context(GTK_WIDGET(row)), "hl");
	}

	if (history_selected < 0) {
		if (direction > 0)
			history_selected = 0;
		else
			history_selected = n - 1;
	} else {
		history_selected += direction;
		if (history_selected < 0)
			history_selected = n - 1;
		if (history_selected >= n)
			history_selected = 0;
	}

	row = gtk_list_box_get_row_at_index(
		GTK_LIST_BOX(history_list), history_selected);
	if (row) {
		gtk_style_context_add_class(
			gtk_widget_get_style_context(GTK_WIDGET(row)), "hl");

		GtkWidget *label = gtk_bin_get_child(GTK_BIN(row));
		if (label) {
			const char *uri = gtk_widget_get_name(label);
			if (uri && *uri) {
				gchar *entry_text = g_strdup_printf(" [COMMAND] %s", uri);
				gtk_entry_set_text(GTK_ENTRY(c->statentry), entry_text);
				gtk_editable_set_position(GTK_EDITABLE(c->statentry), -1);
				g_free(entry_text);
			}
		}
	}
}

void
tab_pin(Client *c, const Arg *a) {
	if (tabs.count <= 0)
		return;

	if (!tab_pins) {
		tab_pins = g_malloc0(tabs.count * sizeof(gboolean));
	}

	tab_pins[tabs.active] = !tab_pins[tabs.active];

	fprintf(stderr, "tab %d %s\n", tabs.active,
		tab_pins[tabs.active] ? "pinned" : "unpinned");

	updatetitle(c);
}

static gboolean
pin_keepalive(gpointer data)
{
	if (!tab_pins)
		return TRUE;

	for (int i = 0; i < tabs.count; i++) {
		if (i == tabs.active)
			continue;
		if (!tab_pins[i])
			continue;
		if (!tabs.views[i])
			continue;

		/* Poke the webview to prevent suspension */
		webkit_web_view_evaluate_javascript(tabs.views[i],
			"void(0);", -1, NULL, NULL, NULL, NULL, NULL);
	}

	return TRUE;
}

int
main(int argc, char *argv[])
{
	Arg arg;
	Client *c;

	/* Force WebKit into single-process mode to fix YouTube scrolling */
	g_setenv("WEBKIT_FORCE_SANDBOX", "0", TRUE);

	memset(&arg, 0, sizeof(arg));

	/* command line args */
	ARGBEGIN {
	case 'a':
		defconfig[CookiePolicies].val.v = EARGF(usage());
		defconfig[CookiePolicies].prio = 2;
		break;
	case 'b':
		defconfig[ScrollBars].val.i = 0;
		defconfig[ScrollBars].prio = 2;
		break;
	case 'B':
		defconfig[ScrollBars].val.i = 1;
		defconfig[ScrollBars].prio = 2;
		break;
	case 'c':
		cookiefile = EARGF(usage());
		break;
	case 'C':
		stylefile = EARGF(usage());
		break;
	case 'd':
		defconfig[DiskCache].val.i = 0;
		defconfig[DiskCache].prio = 2;
		break;
	case 'D':
		defconfig[DiskCache].val.i = 1;
		defconfig[DiskCache].prio = 2;
		break;
	case 'e':
		fprintf(stderr, "Warning: Embedding (-e) is deprecated in Wayland mode\n");
		/* Continue without embedding */
		break;
	case 'f':
		defconfig[RunInFullscreen].val.i = 0;
		defconfig[RunInFullscreen].prio = 2;
		break;
	case 'F':
		defconfig[RunInFullscreen].val.i = 1;
		defconfig[RunInFullscreen].prio = 2;
		break;
	case 'g':
		defconfig[Geolocation].val.i = 0;
		defconfig[Geolocation].prio = 2;
		break;
	case 'G':
		defconfig[Geolocation].val.i = 1;
		defconfig[Geolocation].prio = 2;
		break;
	case 'i':
		defconfig[LoadImages].val.i = 0;
		defconfig[LoadImages].prio = 2;
		break;
	case 'I':
		defconfig[LoadImages].val.i = 1;
		defconfig[LoadImages].prio = 2;
		break;
	case 'k':
		defconfig[KioskMode].val.i = 0;
		defconfig[KioskMode].prio = 2;
		break;
	case 'K':
		defconfig[KioskMode].val.i = 1;
		defconfig[KioskMode].prio = 2;
		break;
	case 'm':
		defconfig[Style].val.i = 0;
		defconfig[Style].prio = 2;
		break;
	case 'M':
		defconfig[Style].val.i = 1;
		defconfig[Style].prio = 2;
		break;
	case 'n':
		defconfig[Inspector].val.i = 0;
		defconfig[Inspector].prio = 2;
		break;
	case 'N':
		defconfig[Inspector].val.i = 1;
		defconfig[Inspector].prio = 2;
		break;
	case 'r':
		scriptfile = EARGF(usage());
		break;
	case 's':
		defconfig[JavaScript].val.i = 0;
		defconfig[JavaScript].prio = 2;
		break;
	case 'S':
		defconfig[JavaScript].val.i = 1;
		defconfig[JavaScript].prio = 2;
		break;
	case 't':
		defconfig[StrictTLS].val.i = 0;
		defconfig[StrictTLS].prio = 2;
		break;
	case 'T':
		defconfig[StrictTLS].val.i = 1;
		defconfig[StrictTLS].prio = 2;
		break;
	case 'u':
		fulluseragent = EARGF(usage());
		break;
	case 'v':
		die("surf-"VERSION", see LICENSE for © details\n");
	case 'w':
		showxidflag = 1;
		break;
	case 'x':
		defconfig[Certificate].val.i = 0;
		defconfig[Certificate].prio = 2;
		break;
	case 'X':
		defconfig[Certificate].val.i = 1;
		defconfig[Certificate].prio = 2;
		break;
	case 'z':
		defconfig[ZoomLevel].val.f = strtof(EARGF(usage()), NULL);
		defconfig[ZoomLevel].prio = 2;
		break;
	default:
		usage();
	} ARGEND;
	if (argc > 0)
		arg.v = argv[0];
	else
		arg.v = "about:blank";

	setup();
	c = newclient(NULL);
	showview(NULL, c);

	loaduri(c, &arg);
	updatetitle(c);

	/* Set up D-Bus event processing for Wayland */
#ifdef WAYLAND_SUPPORT
	if (display_ctx.backend == DISPLAY_BACKEND_WAYLAND) {
		g_timeout_add(50, (GSourceFunc)dbus_process_events, NULL);
		// GTK handles Wayland display events natively through gtk_main()
	}
#endif

	gtk_main();
	cleanup();

	return 0;
}
