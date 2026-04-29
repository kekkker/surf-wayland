/* See LICENSE file for copyright and license details.
 *
 * To understand surf, start reading main().
 */
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
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
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gio/gunixfdlist.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <webkit/webkit.h>

#include "arg.h"
#include "common.h"
#include "display.h"
#include "types.h"

#define LENGTH(x) (sizeof(x) / sizeof(x[0]))
#define CLEANMASK(mask) (mask & (MODKEY | GDK_SHIFT_MASK))

enum {
	OnDoc = WEBKIT_HIT_TEST_RESULT_CONTEXT_DOCUMENT,
	OnLink = WEBKIT_HIT_TEST_RESULT_CONTEXT_LINK,
	OnImg = WEBKIT_HIT_TEST_RESULT_CONTEXT_IMAGE,
	OnMedia = WEBKIT_HIT_TEST_RESULT_CONTEXT_MEDIA,
	OnEdit = WEBKIT_HIT_TEST_RESULT_CONTEXT_EDITABLE,
	OnBar = WEBKIT_HIT_TEST_RESULT_CONTEXT_SCROLLBAR,
	OnSel = WEBKIT_HIT_TEST_RESULT_CONTEXT_SELECTION,
	OnAny = OnDoc | OnLink | OnImg | OnMedia | OnEdit | OnBar | OnSel,
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
	PrivacyMode,
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
static void crashhandler(int sig, siginfo_t *info, void *ctx);
static char *buildfile(const char *path);
static char *buildpath(const char *path);
static char *untildepath(const char *path);
static const char *getuserhomedir(const char *user);
static const char *getcurrentuserhomedir(void);
static Client *newclient(Client *c);
static void loaduri(Client *c, const Arg *a);
static const char *geturi(Client *c);
static void updatetitle(Client *c);
static WebKitCookieAcceptPolicy cookiepolicy_get(void);
static char cookiepolicy_set(const WebKitCookieAcceptPolicy p) __attribute__((unused));
static void seturiparameters(Client *c, const char *uri, ParamName *params);
static void setparameter(Client *c, int refresh, ParamName p, const Arg *a);
static const char *getcert(const char *uri);
static void setcert(Client *c, const char *file);
static const char *getstyle(const char *uri);
static void setstyle(Client *c, const char *file);
static void runscript(Client *c);
static void evalscript(Client *c, const char *jsstr, ...);
static void updateinstanceid(Client *c);
static void handleplumb(Client *c, const char *uri);
static void newwindow(Client *c, const Arg *a, int noembed);
static void spawn(Client *c, const Arg *a);
static void destroyclient(Client *c);
static void cleanup(void);

/* GTK/WebKit */
static WebKitWebView *newview(Client *c, WebKitWebView *rv);
static void initwebextensions(WebKitWebContext *wc, Client *c);
static GtkWidget *createview(WebKitWebView *v, WebKitNavigationAction *a,
							 Client *c);
static void buttonreleased(GtkGestureClick *gesture, int n_press, double x, double y, Client *c);
static gboolean winevent_key(GtkEventControllerKey *ctrl, guint keyval, guint keycode, GdkModifierType state, Client *c);
static void winevent_enter(GtkEventControllerMotion *ctrl, double x, double y, Client *c);
static void winevent_leave(GtkEventControllerMotion *ctrl, Client *c);
static void winevent_fullscreen(GObject *win, GParamSpec *pspec, Client *c);
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
static void downloadstarted(WebKitNetworkSession *ns, WebKitDownload *d,
							Client *c);
static gboolean decidedestination(WebKitDownload *d,
								  gchar *suggested_filename, Client *c);
static void dlprogress(WebKitDownload *d, GParamSpec *ps, gpointer unused);
static void dlfinished(WebKitDownload *d, gpointer unused);
static void dlfailed(WebKitDownload *d, GError *err, gpointer unused);
static void dl_clear(Client *c, const Arg *a);

typedef struct {
	guint index;	   /* 1-based display number */
	gchar *name;	   /* suggested filename */
	guint64 prev_recv; /* bytes received at last speed sample */
	gdouble prev_time; /* elapsed seconds at last speed sample */
	gdouble speed;	   /* smoothed bytes/sec */
	gint64 total;	   /* content-length, -1 if unknown */
	gboolean done;
	GtkWidget *label;
} DlData;
static gboolean viewusrmsgrcv(WebKitWebView *v, WebKitUserMessage *m,
							  gpointer u);
static void webprocessterminated(WebKitWebView *v,
								 WebKitWebProcessTerminationReason r,
								 Client *c);
static void closeview(WebKitWebView *v, Client *c);
static void destroywin(GtkWidget *w, Client *c);
static gboolean filechooser(WebKitWebView *v, WebKitFileChooserRequest *r,
							Client *c);

/* Hotkeys */
static void pasteuri(GObject *clipboard, GAsyncResult *result, gpointer d);
static void reload(Client *c, const Arg *a);
static void print(Client *c, const Arg *a);
static void screenshot(Client *c, const Arg *a);
static void showcert(Client *c, const Arg *a);
static void clipboard(Client *c, const Arg *a);
static void zoom(Client *c, const Arg *a);
static void scrollv(Client *c, const Arg *a);
static void scrollh(Client *c, const Arg *a) __attribute__((unused));
static void navigate(Client *c, const Arg *a);
static void reloaduserscripts(Client *c, const Arg *a);
static void stop(Client *c, const Arg *a);
static void toggle(Client *c, const Arg *a);
static void togglefullscreen(Client *c, const Arg *a);
static void togglecookiepolicy(Client *c, const Arg *a);
static void toggleinspector(Client *c, const Arg *a);
static void find(Client *c, const Arg *a);
static void opensearch(Client *c, const Arg *a);
static void showinstanceid(Client *c, const Arg *a);
static void toggleinsert(Client *c, const Arg *a);
static void openbar(Client *c, const Arg *a);
static void closebar(Client *c);
static void updatebar(Client *c);
static void updatebar_style(Client *c);
static void baractivate(GtkEntry *entry, Client *c);
static gboolean barkeypress(GtkEventControllerKey *ctrl, guint keyval, guint keycode, GdkModifierType state, Client *c);
static void bar_on_changed(GtkEditable *editable, Client *c);
static gboolean bar_update_search(gpointer data);
static void find_highlight_update(Client *c, const char *needle);
static void find_select_enter(Client *c, const Arg *a);
static void find_select_line(Client *c, const Arg *a);
static void find_select_exit(Client *c);
static void find_select_yank_cb(GObject *obj, GAsyncResult *res, gpointer data);

/* Tab management */
static Tab *tab_alloc(Client *c);
static void tab_switch_to(Client *c, int index);
static void tab_new(Client *c, const Arg *a);
static void tab_close(Client *c, const Arg *a);
static void tab_next(Client *c, const Arg *a);
static void tab_prev(Client *c, const Arg *a);
static void openbar_newtab(Client *c, const Arg *a);

/* Hints */
static void hints_start(Client *c, const Arg *a);
static void hints_cleanup(Client *c);
static gboolean hints_keypress(Client *c, guint keyval, GdkModifierType state);
static void hints_receive_data(Client *c, GVariant *data);

/* Buttons */
static void clicknavigate(Client *c, const Arg *a, WebKitHitTestResult *h);
static void clicknewwindow(Client *c, const Arg *a, WebKitHitTestResult *h) __attribute__((unused));
static void clicknewtab(Client *c, const Arg *a, WebKitHitTestResult *h);
static void clickexternplayer(Client *c, const Arg *a, WebKitHitTestResult *h);

static char instanceidbuf[64];
static char togglestats[11] __attribute__((unused));
static char pagestats[2] __attribute__((unused));
static display_context_t display_ctx;
static int showinstanceidflag = 0;
static int cookiepolicy;
Client *clients;

/* Userscript support */
static GMainLoop *mainloop;
static WebKitUserContentManager *shared_content_manager;
static Client *find_client_for_view(WebKitWebView *v, Client *hint);
static void setup_fifo(Client *c);
static void spawnuserscript(Client *c, const Arg *a);
static void add_global_stylesheets(WebKitUserContentManager *cm);
static void inject_userscripts_early(WebKitUserContentManager *cm, const char *uri);

static void findcountchanged(WebKitFindController *f, guint count, Client *c);
static void foundtext(WebKitFindController *f, guint count, Client *c);
static void findfailed(WebKitFindController *f, Client *c);

static GArray *history_entries = NULL;
static char *historyfile;
static time_t history_mtime = 0;
static void history_add(const char *uri, const char *title);
static void history_load(void);
static void history_filter(Client *c, const char *text);
static void history_select(Client *c, int direction);
static void history_hide(Client *c);
static void history_attach(Client *c);
static gboolean bar_update_filter(gpointer data) __attribute__((unused));
static void tab_pin(Client *c, const Arg *a);
static void tab_reopen(Client *c, const Arg *a);
static void tab_move(Client *c, const Arg *a);
static void reapply_stylesheets(Client *c);
static void add_privacy_stylesheet(WebKitUserContentManager *cm);

static guint pin_timer = 0;
static gboolean pin_keepalive(gpointer data);

typedef struct {
	char *uri;
	char *title;
	long timestamp;
} HistoryEntry;

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
	ParameterLast};

static ParamName loadcommitted[] = {
	CaretBrowsing,
	DarkMode,
	DefaultCharset,
	FontSize,
	Geolocation,
	HideBackground,
	Inspector,
	MediaManualPlay,
	PDFJSviewer,
	PrivacyMode,
	RunInFullscreen,
	ScrollBars,
	SiteQuirks,
	SmoothScrolling,
	SpellChecking,
	SpellLanguages,
	Style,
	ZoomLevel,
	ParameterLast};

static ParamName loadfinished[] = {
	ParameterLast};

/* configuration, allows nested code to access above variables */
#include "config.h"

static void
die(const char *errstr, ...)
{
	va_list ap;
	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(1);
}

static void
usage(void)
{
	die("usage: surf [-bBdDfFgGiIkKmMnNpPsStTvwxX]\n"
		"[-a cookiepolicies ] [-c cookiefile] [-C stylefile]\n"
		"[-r scriptfile] [-u useragent] [-z zoomlevel] [uri]\n");
}

static void
setup(void)
{
	GdkDisplay *gdpy;
	int i, j;

	sigchld(0);
	if (signal(SIGHUP, sighup) == SIG_ERR)
		die("Can't install SIGHUP handler");

	{
		struct sigaction sa;
		memset(&sa, 0, sizeof(sa));
		sigemptyset(&sa.sa_mask);
		sa.sa_sigaction = crashhandler;
		sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
		sigaction(SIGSEGV, &sa, NULL);
		sigaction(SIGABRT, &sa, NULL);
		sigaction(SIGBUS, &sa, NULL);
	}

	gtk_init();

	gdpy = gdk_display_get_default();
	if (!gdpy)
		die("Failed to get GDK display");

	if (display_init_with_gdk_display(&display_ctx, gdpy) != 0)
		die("Failed to initialize display backend");

	curconfig = defconfig;

	/* dirs and files */
	cookiefile = buildfile(cookiefile);
	historyfile = buildfile("~/.surf/history");
	scriptfile = buildfile(scriptfile);
	certdir = buildpath(certdir);
	if (curconfig[Ephemeral].val.i)
		cachedir = NULL;
	else
		cachedir = buildpath(cachedir);

	mainloop = g_main_loop_new(NULL, FALSE);

	if (socketpair(AF_UNIX, SOCK_DGRAM, 0, spair) < 0) {
		fputs("Unable to create sockets\n", stderr);
		spair[0] = spair[1] = -1;
	} else {
		GIOChannel *gchanin = g_io_channel_unix_new(spair[0]);
		g_io_channel_set_encoding(gchanin, NULL, NULL);
		g_io_channel_set_flags(gchanin, g_io_channel_get_flags(gchanin) | G_IO_FLAG_NONBLOCK, NULL);
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

		for (j = 0; j < ParameterLast; ++j) {
			if (defconfig[j].prio >= uriparams[i].config[j].prio)
				uriparams[i].config[j] = defconfig[j];
		}
	}
	pin_timer = g_timeout_add_seconds(5, pin_keepalive, NULL);
}

static void
sigchld(int unused)
{
	if (signal(SIGCHLD, sigchld) == SIG_ERR)
		die("Can't install SIGCHLD handler");
	while (waitpid(-1, NULL, WNOHANG) > 0)
		;
}

static void
sighup(int unused)
{
	Arg a = {.i = 0};
	Client *c;

	for (c = clients; c; c = c->next)
		reload(c, &a);
}

static void
crashhandler(int sig, siginfo_t *info, void *ctx)
{
	void *frames[64];
	int nframes, fd;
	char path[PATH_MAX];
	char buf[128];
	const char *home;
	time_t t;
	ssize_t wr __attribute__((unused));

	(void)ctx;

	home = getenv("HOME");
	if (!home)
		home = "/tmp";
	snprintf(path, sizeof(path), "%s/.surf/crash.log", home);

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		fd = STDERR_FILENO;

	t = time(NULL);
	snprintf(buf, sizeof(buf), "surf crash: signal %d at %s", sig, ctime(&t));
	wr = write(fd, buf, strlen(buf));

	if (info && (sig == SIGSEGV || sig == SIGBUS)) {
		snprintf(buf, sizeof(buf), "fault addr: %p\n", info->si_addr);
		wr = write(fd, buf, strlen(buf));
	}

	wr = write(fd, "backtrace:\n", 11);
	nframes = backtrace(frames, 64);
	backtrace_symbols_fd(frames, nframes, fd);

	if (fd != STDERR_FILENO)
		close(fd);

	signal(sig, SIG_DFL);
	raise(sig);
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

	g_chmod(fpath, 0600);
	fclose(f);

	return fpath;
}

static const char *
getuserhomedir(const char *user)
{
	struct passwd *pw = getpwnam(user);

	if (!pw)
		die("Can't get user %s login information.\n", user);

	return pw->pw_dir;
}

static const char *
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

static void
generate_instance_id(Client *c)
{
	snprintf(c->instance_id, sizeof(c->instance_id),
			 "surf-%ld-%u", (long)getpid(), g_random_int());
}

/* Allocate and zero-initialise a new Tab slot appended to c->tabs. */
static Tab *
tab_alloc(Client *c)
{
	c->tabs_count++;
	c->tabs = g_realloc(c->tabs, c->tabs_count * sizeof(Tab));
	Tab *t = &c->tabs[c->tabs_count - 1];
	memset(t, 0, sizeof(Tab));
	t->progress = 100;
	t->mode = ModeNormal;
	return t;
}

Client *
newclient(Client *rc)
{
	Client *c;

	if (!(c = calloc(1, sizeof(Client))))
		die("Cannot malloc!\n");

	c->next = clients;
	clients = c;

	/* Bootstrap the first tab */
	Tab *t = tab_alloc(c);
	c->tabs_active = 0;
	/* rc is non-NULL when creating a related view (popup/tab); pass its active view */
	t->view = newview(c, rc ? ctab(rc)->view : NULL);

	return c;
}

static void
loaduri(Client *c, const Arg *a)
{
	struct stat st;
	char *url, *path, *apath, *encoded;
	const char *uri = a->v;

	if (g_strcmp0(uri, "") == 0)
		return;

	if (g_str_has_prefix(uri, "http://") ||
		g_str_has_prefix(uri, "https://") ||
		g_str_has_prefix(uri, "file://") ||
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
			url = g_strdup_printf("https://%s", uri);
		} else {
			encoded = g_uri_escape_string(uri, NULL, TRUE);
			url = g_strdup_printf(searchengine, encoded);
			g_free(encoded);
		}
		if (apath != uri)
			free(apath);
	}

	if (strcmp(url, geturi(c)) == 0) {
		reload(c, a);
	} else {
		webkit_web_view_load_uri(ctab(c)->view, url);
		updatetitle(c);
	}

	g_free(url);
}

const char *
geturi(Client *c)
{
	const char *uri;

	if (!(uri = webkit_web_view_get_uri(ctab(c)->view)))
		uri = "about:blank";
	return uri;
}

static void
tabbar_click(GtkGestureClick *gesture, int n_press, double x, double y, Client *c)
{
	GtkWidget *w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
	int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "tab-index"));
	guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

	if (button == 2) {
		tab_switch_to(c, idx);
		tab_close(c, &(Arg){0});
	} else if (button == 1) {
		tab_switch_to(c, idx);
	}
}

static void
update_tabbar(Client *c)
{
	GtkWidget *child;
	int i;

	if (!c->tabbar)
		return;

	while ((child = gtk_widget_get_first_child(c->tabbar)))
		gtk_box_remove(GTK_BOX(c->tabbar), child);

	for (i = 0; i < c->tabs_count; i++) {
		Tab *t = &c->tabs[i];
		/* Prefer the cached title, fall back to live WebKit title */
		const char *title = t->title && *t->title ? t->title
		                    : webkit_web_view_get_title(t->view);
		const char *uri   = webkit_web_view_get_uri(t->view);
		GtkWidget *label, *box;
		GtkGesture *gesture;
		gchar *text;

		if (!title || !*title)
			title = (uri && !g_str_has_prefix(uri, "about:")) ? uri : "New Tab";

		gchar *valid_title = g_utf8_make_valid(title, -1);

		if (t->pinned) {
			text = g_strdup_printf("[P] %s", valid_title);
			g_free(valid_title);
		} else {
			text = valid_title;
		}

		label = gtk_label_new(text);
		gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
		gtk_label_set_single_line_mode(GTK_LABEL(label), TRUE);
		gtk_widget_set_hexpand(label, TRUE);
		g_free(text);

		gtk_label_set_xalign(GTK_LABEL(label), 0.0);
		gtk_widget_set_margin_start(label, 8);
		gtk_widget_set_margin_end(label, 8);

		box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		gtk_widget_set_hexpand(box, TRUE);
		gtk_box_append(GTK_BOX(box), label);
		g_object_set_data(G_OBJECT(box), "tab-index", GINT_TO_POINTER(i));
		gesture = gtk_gesture_click_new();
		gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);
		g_signal_connect(gesture, "pressed", G_CALLBACK(tabbar_click), c);
		gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(gesture));
		gtk_widget_set_name(box, i == c->tabs_active ? "tab-active" : "tab-inactive");
		gtk_box_append(GTK_BOX(c->tabbar), box);
	}
}

static void
tab_switch_to(Client *c, int index)
{
	Tab *prev_t, *next_t;

	if (index < 0 || index >= c->tabs_count || index == c->tabs_active)
		return;

	next_t = &c->tabs[index];
	if (next_t->view == NULL)
		return;

	/* Hide current tab's view widget */
	if (c->tabs_active >= 0 && c->tabs_active < c->tabs_count) {
		prev_t = &c->tabs[c->tabs_active];
		if (prev_t->view)
			gtk_widget_set_visible(GTK_WIDGET(prev_t->view), FALSE);
	}

	c->tabs_active = index;

	/* Sync window-level inspector pointer to the newly active tab */
	c->inspector = webkit_web_view_get_inspector(next_t->view);

	/* Sync live WebKit state into the Tab (progress, TLS) in case we
	 * missed signals while this tab was hidden. */
	next_t->progress = webkit_web_view_get_estimated_load_progress(next_t->view) * 100;
	next_t->https    = webkit_web_view_get_tls_info(next_t->view,
	                                                 &next_t->cert,
	                                                 &next_t->tlserr);

	/* Reset any transient UI mode to Normal on switch */
	next_t->mode = ModeNormal;
	gtk_widget_set_focusable(c->statentry, FALSE);
	gtk_editable_set_editable(GTK_EDITABLE(c->statentry), FALSE);

	gtk_widget_set_visible(GTK_WIDGET(next_t->view), TRUE);
	gtk_widget_grab_focus(GTK_WIDGET(next_t->view));

	updatetitle(c);
	update_tabbar(c);
}

static void
tab_new(Client *c, const Arg *a)
{
	int insert_at = c->tabs_active + 1;

	/* Hide the currently visible view */
	if (c->tabs_active >= 0 && c->tabs_active < c->tabs_count &&
	    c->tabs[c->tabs_active].view)
		gtk_widget_set_visible(GTK_WIDGET(c->tabs[c->tabs_active].view), FALSE);

	/* Insert a new Tab slot after the active one */
	c->tabs_count++;
	c->tabs = g_realloc(c->tabs, c->tabs_count * sizeof(Tab));
	/* Shift tabs right to make room at insert_at */
	memmove(&c->tabs[insert_at + 1], &c->tabs[insert_at],
	        (c->tabs_count - 1 - insert_at) * sizeof(Tab));
	Tab *t = &c->tabs[insert_at];
	memset(t, 0, sizeof(Tab));
	t->progress = 100;
	t->mode = ModeNormal;

	/* Create the WebView related to the current active view */
	t->view = newview(c, c->tabs[c->tabs_active == insert_at
	                               ? insert_at + 1 /* original active shifted */
	                               : c->tabs_active].view);

	gtk_widget_set_vexpand(GTK_WIDGET(t->view), TRUE);
	gtk_widget_set_hexpand(GTK_WIDGET(t->view), TRUE);
	gtk_box_insert_child_after(GTK_BOX(c->vbox), GTK_WIDGET(t->view),
	                           gtk_widget_get_prev_sibling(c->statusbar));

	t->pageid = webkit_web_view_get_page_id(t->view);
	t->finder = webkit_web_view_get_find_controller(t->view);

	/* Connect find signals for this new view */
	g_signal_connect(t->finder, "counted-matches",
	                 G_CALLBACK(findcountchanged), c);
	g_signal_connect(t->finder, "found-text",
	                 G_CALLBACK(foundtext), c);
	g_signal_connect(t->finder, "failed-to-find-text",
	                 G_CALLBACK(findfailed), c);

	c->tabs_active = insert_at;
	c->inspector = webkit_web_view_get_inspector(t->view);

	gtk_widget_set_visible(GTK_WIDGET(t->view), TRUE);
	gtk_widget_set_focusable(c->statentry, FALSE);
	gtk_editable_set_editable(GTK_EDITABLE(c->statentry), FALSE);
	gtk_widget_grab_focus(GTK_WIDGET(t->view));
	updatetitle(c);
	update_tabbar(c);

	if (a && a->i == 0) {
		Arg bar = {.i = 0};
		openbar(c, &bar);
	}
}

static void
tab_close(Client *c, const Arg *a)
{
	int idx;

	if (c->tabs_count <= 1)
		return;

	idx = c->tabs_active;

	/* Take ownership of view for cleanup after removing from array */
	Tab dying = c->tabs[idx];
	g_object_ref(dying.view);  /* ref for cleanup after removal */

	webkit_web_view_stop_loading(dying.view);
	gtk_widget_set_visible(GTK_WIDGET(dying.view), FALSE);

	/* Shift tabs left over the closed slot */
	memmove(&c->tabs[idx], &c->tabs[idx + 1],
	        (c->tabs_count - 1 - idx) * sizeof(Tab));
	c->tabs_count--;
	c->tabs = g_realloc(c->tabs, c->tabs_count * sizeof(Tab));

	/* Pick neighbour to switch to; set active to -1 first so
	 * tab_switch_to doesn't skip (index == tabs_active). */
	int new_idx = idx < c->tabs_count ? idx : c->tabs_count - 1;
	c->tabs_active = -1;
	tab_switch_to(c, new_idx);

	/* Disconnect all signals connected with c as data (this view only) */
	g_signal_handlers_disconnect_matched(dying.view,
	                                     G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, c);

	/* Push onto closed-tab stack */
	const char *dead_uri = webkit_web_view_get_uri(dying.view);
	if (dead_uri && strcmp(dead_uri, "about:blank") != 0) {
		if (c->closed_tab_top == CLOSED_TAB_MAX) {
			g_free(c->closed_tab_stack[0]);
			memmove(c->closed_tab_stack, c->closed_tab_stack + 1,
			        (CLOSED_TAB_MAX - 1) * sizeof(char *));
			c->closed_tab_top--;
		}
		c->closed_tab_stack[c->closed_tab_top++] = g_strdup(dead_uri);
	}

	/* Free per-tab owned strings */
	g_free(dying.title);
	g_free(dying.targeturi);
	if (dying.mousepos)
		g_object_unref(dying.mousepos);

	/* Remove view from parent widget and drop our reference */
	gtk_box_remove(GTK_BOX(c->vbox), GTK_WIDGET(dying.view));
	g_object_unref(dying.view);
	update_tabbar(c);
}

static void
tab_reopen(Client *c, const Arg *a)
{
	if (c->closed_tab_top == 0)
		return;
	char *uri = c->closed_tab_stack[--c->closed_tab_top];
	tab_new(c, &(Arg){.i = 1});
	loaduri(c, &(Arg){.v = uri});
	g_free(uri);
	c->closed_tab_stack[c->closed_tab_top] = NULL;
}

static void
tab_next(Client *c, const Arg *a)
{
	(void)a;
	if (c->tabs_count <= 1)
		return;
	int next = (c->tabs_active + 1) % c->tabs_count;
	tab_switch_to(c, next);
}

static void
tab_prev(Client *c, const Arg *a)
{
	if (c->tabs_count <= 1)
		return;
	int prev = (c->tabs_active - 1 + c->tabs_count) % c->tabs_count;
	tab_switch_to(c, prev);
}

static void
tab_move(Client *c, const Arg *a)
{
	int idx     = c->tabs_active;
	int new_idx = idx + a->i;

	if (new_idx < 0 || new_idx >= c->tabs_count)
		return;

	Tab tmp = c->tabs[idx];
	c->tabs[idx] = c->tabs[new_idx];
	c->tabs[new_idx] = tmp;

	c->tabs_active = new_idx;
	update_tabbar(c);
}

/* Hint label characters (home row keys) */
static const char *hintkeys = "asdfghjkl";

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

static void
request_hints_from_extension(Client *c)
{
	WebKitUserMessage *msg;
	msg = webkit_user_message_new("hints-find-links", NULL);
	webkit_web_view_send_message_to_page(ctab(c)->view, msg, NULL, NULL, NULL);
}

static void
hints_cleanup(Client *c)
{
	HintState *hs = &ctab(c)->hintstate;

	if (!hs->active)
		return;

	/* Always try to clear the overlay */
	WebKitUserMessage *msg = webkit_user_message_new("hints-clear", NULL);
	webkit_web_view_send_message_to_page(ctab(c)->view, msg, NULL, NULL, NULL);

	if (hs->hints) {
		for (guint i = 0; i < hs->hints->len; i++) {
			Hint *h = &g_array_index(hs->hints, Hint, i);
			g_free(h->label);
			g_free(h->url);
		}
		g_array_free(hs->hints, TRUE);
		hs->hints = NULL;
	}

	g_free(hs->input);
	hs->input = NULL;

	hs->active = 0;

	ctab(c)->mode = ModeNormal;
	updatetitle(c);
}

static void
hints_start(Client *c, const Arg *a)
{
	HintState *hs = &ctab(c)->hintstate;

	if (hs->active)
		hints_cleanup(c);

	hs->mode = a->i;
	hs->active = 1;
	hs->pageid = ctab(c)->pageid;
	hs->hints = g_array_new(FALSE, TRUE, sizeof(Hint));
	hs->input = g_strdup("");

	ctab(c)->mode = ModeHint;

	request_hints_from_extension(c);
	updatetitle(c);
}

static void
filter_hints(Client *c)
{
	GVariantBuilder builder;
	HintState *hs = &ctab(c)->hintstate;

	if (!hs->hints)
		return;

	g_variant_builder_init(&builder, G_VARIANT_TYPE("a(ssii)"));

	for (guint i = 0; i < hs->hints->len; i++) {
		Hint *h = &g_array_index(hs->hints, Hint, i);
		if (g_str_has_prefix(h->label, hs->input)) {
			g_variant_builder_add(&builder, "(ssii)",
								  h->label, h->url, h->x, h->y);
		}
	}

	GVariant *hints_data = g_variant_builder_end(&builder);
	WebKitUserMessage *msg = webkit_user_message_new("hints-update", hints_data);
	webkit_web_view_send_message_to_page(ctab(c)->view, msg, NULL, NULL, NULL);
}

static void
follow_hint(Client *c, const char *label)
{
	HintState *hs = &ctab(c)->hintstate;
	Hint *target = NULL;

	for (guint i = 0; i < hs->hints->len; i++) {
		Hint *h = &g_array_index(hs->hints, Hint, i);
		if (strcmp(h->label, label) == 0) {
			target = h;
			break;
		}
	}

	if (!target) {
		hints_cleanup(c);
		return;
	}

	if (g_str_has_prefix(target->url, "[input:")) {
		guint elem_id;
		if (sscanf(target->url, "[input:%u]", &elem_id) == 1) {
			GVariant *data = g_variant_new("(u)", elem_id);
			WebKitUserMessage *msg = webkit_user_message_new("hints-click", data);
			webkit_web_view_send_message_to_page(ctab(c)->view, msg, NULL, NULL, NULL);
		}
		hints_cleanup(c);
		ctab(c)->mode = ModeInsert;
		updatetitle(c);
		return;
	}

	if (g_str_has_prefix(target->url, "[elem:")) {
		guint elem_id;
		if (sscanf(target->url, "[elem:%u]", &elem_id) == 1) {
			GVariant *data = g_variant_new("(u)", elem_id);
			WebKitUserMessage *msg = webkit_user_message_new("hints-click", data);
			webkit_web_view_send_message_to_page(ctab(c)->view, msg, NULL, NULL, NULL);
		}
		hints_cleanup(c);
		return;
	}

	Arg arg;
	switch (hs->mode) {
	case HintModeLink: {
		gchar *url_copy = g_strdup(target->url);
		hints_cleanup(c);
		arg.v = url_copy;
		loaduri(c, &arg);
		g_free(url_copy);
	} break;
	case HintModeNewWindow: {
		gchar *url_copy = g_strdup(target->url);
		int prev = c->tabs_active;

		hints_cleanup(c); /* clears hints on old view, resets state */
		tab_new(c, &(Arg){.i = 1});
		arg.v = url_copy;
		loaduri(c, &arg);
		g_free(url_copy);
		tab_switch_to(c, prev);
	} break;
	case HintModeYank: {
		gchar *url_copy = g_strdup(target->url);
		GdkDisplay *dpy = gdk_display_get_default();

		gdk_clipboard_set_text(gdk_display_get_clipboard(dpy), url_copy);
		gdk_clipboard_set_text(gdk_display_get_primary_clipboard(dpy), url_copy);

		hints_cleanup(c);
		g_free(url_copy);
	} break;
	}
}

static gboolean
hints_keypress(Client *c, guint keyval, GdkModifierType state)
{
	HintState *hs = &ctab(c)->hintstate;
	char key;
	char *newinput;
	int label_len;

	if (!hs->active || hs->pageid != ctab(c)->pageid)
		return FALSE;

	if (keyval == GDK_KEY_Escape) {
		hints_cleanup(c);
		return TRUE;
	}

	if (keyval == GDK_KEY_BackSpace) {
		int len = strlen(hs->input);
		if (len > 0) {
			hs->input[len - 1] = '\0';
			filter_hints(c);
		} else {
			hints_cleanup(c); /* No input left, exit hints */
		}
		return TRUE;
	}

	key = gdk_keyval_to_lower(keyval);
	if (!strchr(hintkeys, key))
		return TRUE;

	newinput = g_strdup_printf("%s%c", hs->input, key);
	g_free(hs->input);
	hs->input = newinput;

	if (hs->hints->len == 0) {
		hints_cleanup(c); /* No hints available */
		return TRUE;
	}

	label_len = strlen(g_array_index(hs->hints, Hint, 0).label);

	int matches = 0;
	char *matched_label = NULL;

	for (guint i = 0; i < hs->hints->len; i++) {
		Hint *h = &g_array_index(hs->hints, Hint, i);
		if (g_str_has_prefix(h->label, hs->input)) {
			matches++;
			matched_label = h->label;
		}
	}

	if (matches == 0) {
		hints_cleanup(c); /* ADD: No matches, cleanup */
	} else if ((int)strlen(hs->input) == label_len && matches == 1) {
		follow_hint(c, matched_label);
		/* hints_cleanup is called inside follow_hint */
	} else {
		filter_hints(c);
	}

	return TRUE;
}

static void
hints_receive_data(Client *c, GVariant *data)
{
	GVariantIter iter;
	HintState *hs = &ctab(c)->hintstate;
	const gchar *url;
	gint x, y, width, height;
	int index = 0;
	int total;

	if (!hs->active)
		return;

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

		g_array_append_val(hs->hints, hint);
	}

	filter_hints(c);
}

static void
updatetitle(Client *c)
{
	char *title;
	const char *name;
	const char *pin;
	Tab *t = ctab(c);

	if (t->hover_link)
		name = t->targeturi;
	else if (t->title)
		name = t->title;
	else
		name = "";

	pin = t->pinned ? "[P]" : "";

	if (c->tabs_count > 1) {
		title = g_strdup_printf("[%d/%d]%s %s",
		                        c->tabs_active + 1, c->tabs_count, pin, name);
	} else {
		title = g_strdup_printf("%s%s", name, pin);
	}

	gtk_window_set_title(GTK_WINDOW(c->win), title);
	g_free(title);

	updatebar(c);
	update_tabbar(c);
}

WebKitCookieAcceptPolicy
cookiepolicy_get(void)
{
	switch (((char *)curconfig[CookiePolicies].val.v)[cookiepolicy]) {
	case 'a':
		return WEBKIT_COOKIE_POLICY_ACCEPT_NEVER;
	case '@':
		return WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY;
	default:
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
	default:
	case WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS:
		return 'A';
	}
}

static void
seturiparameters(Client *c, const char *uri, ParamName *params)
{
	Parameter *uriconfig = NULL;
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
		if (p != Certificate && p != CookiePolicies && p != Style &&
		    !(defconfig[p].prio < curconfig[p].prio ||
		      defconfig[p].prio < modparams[p]))
			continue;

		switch (p) {
		case Certificate:
		case CookiePolicies:
		case Style:
		default:
			setparameter(c, 0, p, &curconfig[p].val);
		}
	}
}

static void
setparameter(Client *c, int refresh, ParamName p, const Arg *a)
{
	GdkRGBA bgcolor = {0};
	Tab *t = ctab(c);
	WebKitSettings *settings = webkit_web_view_get_settings(t->view);

	modparams[p] = curconfig[p].prio;

	switch (p) {
	case AccessMicrophone:
		return;
	case AccessWebcam:
		return;
	case CaretBrowsing:
		webkit_settings_set_enable_caret_browsing(settings, a->i);
		refresh = 0;
		break;
	case Certificate:
		if (a->i)
			setcert(c, geturi(c));
		return;
	case CookiePolicies:
		webkit_cookie_manager_set_accept_policy(
			webkit_network_session_get_cookie_manager(
				webkit_web_view_get_network_session(t->view)),
			cookiepolicy_get());
		refresh = 0;
		break;
	case DarkMode:
		g_object_set(gtk_settings_get_default(),
		             "gtk-application-prefer-dark-theme", a->i, NULL);
		return;
	case DiskCache:
		webkit_web_context_set_cache_model(c->context,
		    a->i ? WEBKIT_CACHE_MODEL_WEB_BROWSER
		         : WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);
		return;
	case DefaultCharset:
		webkit_settings_set_default_charset(settings, a->v);
		return;
	case DNSPrefetch:
		g_object_set(settings, "enable-dns-prefetching", (gboolean)a->i, NULL);
		return;
	case FileURLsCrossAccess:
		webkit_settings_set_allow_file_access_from_file_urls(settings, a->i);
		webkit_settings_set_allow_universal_access_from_file_urls(settings, a->i);
		return;
	case FontSize:
		webkit_settings_set_default_font_size(settings, a->i);
		return;
	case Geolocation:
		refresh = 0;
		break;
	case HideBackground:
		if (a->i)
			webkit_web_view_set_background_color(t->view, &bgcolor);
		return;
	case Inspector:
		webkit_settings_set_enable_developer_extras(settings, a->i);
		return;
	case JavaScript:
		webkit_settings_set_enable_javascript(settings, a->i);
		break;
	case KioskMode:
		return;
	case LoadImages:
		webkit_settings_set_auto_load_images(settings, a->i);
		break;
	case MediaManualPlay:
		webkit_settings_set_media_playback_requires_user_gesture(settings, a->i);
		break;
	case PDFJSviewer:
		return;
	case PreferredLanguages:
		return;
	case RunInFullscreen:
		return;
	case ScrollBars:
		return;
	case ShowIndicators:
		break;
	case SmoothScrolling:
		webkit_settings_set_enable_smooth_scrolling(settings, a->i);
		return;
	case SiteQuirks:
		webkit_settings_set_enable_site_specific_quirks(settings, a->i);
		break;
	case SpellChecking:
		webkit_web_context_set_spell_checking_enabled(c->context, a->i);
		return;
	case SpellLanguages:
		return;
	case StrictTLS:
		webkit_network_session_set_tls_errors_policy(
			webkit_web_view_get_network_session(t->view),
			a->i ? WEBKIT_TLS_ERRORS_POLICY_FAIL : WEBKIT_TLS_ERRORS_POLICY_IGNORE);
		break;
	case Style:
		reapply_stylesheets(c);
		refresh = 0;
		break;
	case PrivacyMode:
		reapply_stylesheets(c);
		refresh = 0;
		break;
	case WebGL:
		webkit_settings_set_enable_webgl(settings, a->i);
		break;
	case ZoomLevel:
		webkit_web_view_set_zoom_level(t->view, a->f);
		return;
	default:
		return;
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

static void
add_global_stylesheets(WebKitUserContentManager *cm)
{
	static const char scroll_override_css[] =
		"html { scroll-behavior: auto !important; }\n"
		"body { scroll-behavior: auto !important; }\n"
		"* { scroll-behavior: auto !important; }\n";
	WebKitUserStyleSheet *stylesheet;

	stylesheet = webkit_user_style_sheet_new(
		scroll_override_css,
		WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
		WEBKIT_USER_STYLE_LEVEL_USER,
		NULL, NULL);
	webkit_user_content_manager_add_style_sheet(cm, stylesheet);
	webkit_user_style_sheet_unref(stylesheet);
}

static void
add_privacy_stylesheet(WebKitUserContentManager *cm)
{
	static const char privacy_css[] =
		"html, body, body * {\n"
		"  background: #000000 !important;\n"
		"  background-image: none !important;\n"
		"  color: #f5f5f5 !important;\n"
		"  border-color: #3a3a3a !important;\n"
		"  box-shadow: none !important;\n"
		"  text-shadow: none !important;\n"
		"  font-family: monospace !important;\n"
		"}\n"
		"a, a * { color: #ffffff !important; }\n"
		"input, textarea, select, button {\n"
		"  background: #050505 !important;\n"
		"  color: #ffffff !important;\n"
		"}\n"
		"img, svg image, video, canvas, picture, iframe {\n"
		"  image-rendering: pixelated !important;\n"
		"  filter: grayscale(1) blur(10px) contrast(0.65) !important;\n"
		"  opacity: 0.22 !important;\n"
		"}\n"
		"[style*=\"background-image\"], [class*=\"thumb\"], [role=\"img\"] {\n"
		"  filter: grayscale(1) blur(10px) contrast(0.65) !important;\n"
		"}\n"
		"pre, code, kbd, samp { color: #ffffff !important; }\n";
	WebKitUserStyleSheet *stylesheet;

	stylesheet = webkit_user_style_sheet_new(
		privacy_css,
		WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
		WEBKIT_USER_STYLE_LEVEL_USER,
		NULL, NULL);
	webkit_user_content_manager_add_style_sheet(cm, stylesheet);
	webkit_user_style_sheet_unref(stylesheet);
}

static void
reapply_stylesheets(Client *c)
{
	WebKitUserContentManager *cm;

	cm = webkit_web_view_get_user_content_manager(ctab(c)->view);
	webkit_user_content_manager_remove_all_style_sheets(cm);
	add_global_stylesheets(cm);

	if (curconfig[Style].val.i)
		setstyle(c, getstyle(geturi(c)));
	if (curconfig[PrivacyMode].val.i)
		add_privacy_stylesheet(cm);
}

static void
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
		const char *slash;
		uri += sizeof("https://") - 1;
		slash = strchr(uri, '/');
		host = slash ? g_strndup(uri, slash - uri) : g_strdup(uri);
		webkit_network_session_allow_tls_certificate_for_host(
			webkit_web_view_get_network_session(ctab(c)->view), cert, host);
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

static void
setstyle(Client *c, const char *file)
{
	gchar *style;

	if (!g_file_get_contents(file, &style, NULL, NULL)) {
		if (!g_file_test(file, G_FILE_TEST_EXISTS))
			return;
		fprintf(stderr, "Could not read style file: %s\n", file);
		return;
	}

	webkit_user_content_manager_add_style_sheet(
		webkit_web_view_get_user_content_manager(ctab(c)->view),
		webkit_user_style_sheet_new(style,
		                            WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
		                            WEBKIT_USER_STYLE_LEVEL_USER,
		                            NULL, NULL));

	g_free(style);
}

static void
runscript(Client *c)
{
	gchar *script;
	gsize l;

	if (g_file_get_contents(scriptfile, &script, &l, NULL) && l)
		evalscript(c, "%s", script);
	g_free(script);
}

static void
evalscript(Client *c, const char *jsstr, ...)
{
	va_list ap;
	gchar *script;

	va_start(ap, jsstr);
	script = g_strdup_vprintf(jsstr, ap);
	va_end(ap);

	webkit_web_view_evaluate_javascript(ctab(c)->view, script, -1,
	                                    NULL, NULL, NULL, NULL, NULL);
	g_free(script);
}

static void
updateinstanceid(Client *c)
{
	snprintf(instanceidbuf, LENGTH(instanceidbuf), "%s", c->instance_id);
}

static void
handleplumb(Client *c, const char *uri)
{
	Arg a = (Arg)PLUMB(uri);
	spawn(c, &a);
}

static void
newwindow(Client *c, const Arg *a, int noembed)
{
	int i = 0;
	const char *cmd[30], *uri;
	const Arg arg = {.v = cmd};

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
	cmd[i++] = curconfig[RunInFullscreen].val.i ? "-F" : "-f";
	cmd[i++] = curconfig[Geolocation].val.i ? "-G" : "-g";
	cmd[i++] = curconfig[LoadImages].val.i ? "-I" : "-i";
	cmd[i++] = curconfig[KioskMode].val.i ? "-K" : "-k";
	cmd[i++] = curconfig[Style].val.i ? "-M" : "-m";
	cmd[i++] = curconfig[Inspector].val.i ? "-N" : "-n";
	cmd[i++] = curconfig[PrivacyMode].val.i ? "-P" : "-p";
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
	if (showinstanceidflag)
		cmd[i++] = "-w";
	cmd[i++] = curconfig[Certificate].val.i ? "-X" : "-x";
	cmd[i++] = "--";
	if ((uri = a->v))
		cmd[i++] = uri;
	cmd[i] = NULL;

	spawn(c, &arg);
}

static void
spawn(Client *c, const Arg *a)
{
	if (fork() == 0) {
		int _maxfd = (int)sysconf(_SC_OPEN_MAX);
		for (int _fd = 3; _fd < _maxfd; _fd++)
			close(_fd);
		setsid();
		execvp(((char **)a->v)[0], (char **)a->v);
		fprintf(stderr, "%s: execvp %s", argv0, ((char **)a->v)[0]);
		perror(" failed");
		exit(1);
	}
}

static void
destroyclient(Client *c)
{
	Client *p, *prev = NULL;

	/* Free per-tab resources */
	for (int i = 0; i < c->tabs_count; i++) {
		Tab *t = &c->tabs[i];
		if (t->view)
			webkit_web_view_stop_loading(t->view);
		g_free(t->title);
		g_free(t->targeturi);
		if (t->mousepos)
			g_object_unref(t->mousepos);
		/* cert/failedcert: cert is borrowed from WebKit, failedcert is owned */
		if (t->failedcert)
			g_object_unref(t->failedcert);
	}
	g_free(c->tabs);

	for (int i = 0; i < c->closed_tab_top; i++)
		g_free(c->closed_tab_stack[i]);
	if (c->fifo_chan)
		g_io_channel_unref(c->fifo_chan);
	if (c->surf_fifo) {
		unlink(c->surf_fifo);
		g_free(c->surf_fifo);
	}

	/* Free history popup widgets */
	if (c->history_list)
		g_object_unref(c->history_list);
	if (c->history_scroll)
		g_object_unref(c->history_scroll);

	/* Unlink client from list using proper prev pointer */
	for (p = clients; p && p != c; prev = p, p = p->next)
		;
	if (prev)
		prev->next = c->next;
	else
		clients = c->next;
	free(c);
}

static void
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
	display_cleanup(&display_ctx);
	g_free(historyfile);
	if (pin_timer)
		g_source_remove(pin_timer);
	g_clear_object(&shared_content_manager);
}

static Client *
find_client_for_view(WebKitWebView *v, Client *hint)
{
	if (hint) {
		for (int i = 0; i < hint->tabs_count; i++) {
			if (hint->tabs[i].view == v)
				return hint;
		}
	}

	for (Client *c = clients; c; c = c->next) {
		if (c == hint)
			continue;
		for (int i = 0; i < c->tabs_count; i++) {
			if (c->tabs[i].view == v)
				return c;
		}
	}

	return NULL;
}

static void
showinstanceid(Client *c, const Arg *arg)
{
	puts(c->instance_id);
}

WebKitWebView *
newview(Client *c, WebKitWebView *rv)
{
	WebKitWebView *v;
	WebKitSettings *settings;
	WebKitWebContext *context;
	WebKitNetworkSession *netsession;
	WebKitCookieManager *cookiemanager;
	GtkGesture *click;

	if (!shared_content_manager) {
		shared_content_manager = webkit_user_content_manager_new();
		add_global_stylesheets(shared_content_manager);
		inject_userscripts_early(shared_content_manager, "");
	}

	if (rv) {
		v = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
										 "related-view", rv,
										 "user-content-manager", shared_content_manager,
										 NULL));
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

		if (strcmp(fulluseragent, "")) {
			webkit_settings_set_user_agent(settings, fulluseragent);
		} else if (surfuseragent) {
			webkit_settings_set_user_agent_with_application_details(
				settings, "Surf", VERSION);
		}
		useragent = webkit_settings_get_user_agent(settings);

		context = webkit_web_context_new();
		g_signal_connect(G_OBJECT(context), "initialize-web-process-extensions",
						 G_CALLBACK(initwebextensions), c);
		webkit_web_context_set_spell_checking_languages(context,
														curconfig[SpellLanguages].val.v);
		webkit_web_context_set_spell_checking_enabled(context,
													  curconfig[SpellChecking].val.i);
		webkit_web_context_set_preferred_languages(context,
												   curconfig[PreferredLanguages].val.v);
		webkit_web_context_set_cache_model(context,
										   curconfig[DiskCache].val.i ? WEBKIT_CACHE_MODEL_WEB_BROWSER
																	  : WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);

		if (curconfig[Ephemeral].val.i) {
			netsession = webkit_network_session_new_ephemeral();
		} else {
			netsession = webkit_network_session_new(cachedir, cachedir);
		}

		webkit_network_session_set_tls_errors_policy(netsession,
													 curconfig[StrictTLS].val.i ? WEBKIT_TLS_ERRORS_POLICY_FAIL
																				: WEBKIT_TLS_ERRORS_POLICY_IGNORE);

		cookiemanager = webkit_network_session_get_cookie_manager(netsession);
		if (!curconfig[Ephemeral].val.i)
			webkit_cookie_manager_set_persistent_storage(cookiemanager,
														 cookiefile, WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);
		webkit_cookie_manager_set_accept_policy(cookiemanager, cookiepolicy_get());

		g_signal_connect(G_OBJECT(netsession), "download-started",
						 G_CALLBACK(downloadstarted), c);

		v = g_object_new(WEBKIT_TYPE_WEB_VIEW,
						 "settings", settings,
						 "user-content-manager", shared_content_manager,
						 "web-context", context,
						 "network-session", netsession,
						 NULL);
		g_object_unref(netsession);
		g_object_unref(context);
	}

	/* Related views do not reliably pick up every setting we care about. */
	webkit_settings_set_enable_developer_extras(settings,
	                                            curconfig[Inspector].val.i);

	g_signal_connect(G_OBJECT(v), "notify::estimated-load-progress",
					 G_CALLBACK(progresschanged), c);
	g_signal_connect(G_OBJECT(v), "notify::title",
					 G_CALLBACK(titlechanged), c);
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
	g_signal_connect(G_OBJECT(v), "run-file-chooser",
					 G_CALLBACK(filechooser), c);

	/* Button release for mouse gesture/hint mode handling */
	click = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click),
											   GTK_PHASE_BUBBLE);
	g_signal_connect(click, "released", G_CALLBACK(buttonreleased), c);
	gtk_widget_add_controller(GTK_WIDGET(v), GTK_EVENT_CONTROLLER(click));

	/* Store the shared WebKit context on the Client (window-level) */
	c->context  = webkit_web_view_get_context(v);
	c->settings = settings;

	setparameter(c, 0, DarkMode, &curconfig[DarkMode].val);

	return v;
}

static void
initwebextensions(WebKitWebContext *wc, Client *c)
{
	webkit_web_context_set_web_process_extensions_directory(wc, WEBEXTDIR);
}

GtkWidget *
createview(WebKitWebView *v, WebKitNavigationAction *a, Client *c)
{
	Client *n;

	switch (webkit_navigation_action_get_navigation_type(a)) {
	case WEBKIT_NAVIGATION_TYPE_OTHER:
		/* WebKit reports most JS-driven popup requests as OTHER.
		 * Do not drop them here, even when they come from a user gesture. */
		n = newclient(c);
		break;
	case WEBKIT_NAVIGATION_TYPE_LINK_CLICKED:
	case WEBKIT_NAVIGATION_TYPE_FORM_SUBMITTED:
	case WEBKIT_NAVIGATION_TYPE_BACK_FORWARD:
	case WEBKIT_NAVIGATION_TYPE_RELOAD:
	case WEBKIT_NAVIGATION_TYPE_FORM_RESUBMITTED:
		n = newclient(c);
		break;
	default:
		return NULL;
	}

	return GTK_WIDGET(ctab(n)->view);
}

static void
buttonreleased(GtkGestureClick *gesture, int n_press, double x, double y, Client *c)
{
	WebKitHitTestResultContext element;
	guint button;
	GdkModifierType state;
	int i;
	Tab *t = ctab(c);

	if (!t->mousepos)
		return;

	element = webkit_hit_test_result_get_context(t->mousepos);

	if (element & OnEdit) {
		t->mode = ModeInsert;
		updatetitle(c);
	}

	button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
	state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));

	for (i = 0; i < LENGTH(buttons); ++i) {
		if (element & buttons[i].target &&
			button == buttons[i].button &&
			CLEANMASK(state) == CLEANMASK(buttons[i].mask) &&
			buttons[i].func) {
			buttons[i].func(c, &buttons[i].arg, t->mousepos);
			return;
		}
	}
}

static gboolean
winevent_key(GtkEventControllerKey *ctrl, guint keyval, guint keycode,
			 GdkModifierType state, Client *c)
{
	int i;
	Tab *t = ctab(c);

	if (curconfig[KioskMode].val.i)
		return FALSE;

	if (t->mode == ModeCommand || t->mode == ModeSearch)
		return FALSE;

	if (t->mode == ModeInsert) {
		if (keyval == GDK_KEY_Escape) {
			t->mode = ModeNormal;
			gtk_widget_grab_focus(GTK_WIDGET(t->view));
			updatetitle(c);
			return TRUE;
		}
		return FALSE;
	}

	if (t->mode == ModeHint)
		return hints_keypress(c, keyval, state);

	if (t->mode == ModeSelect) {
		switch (keyval) {
		case GDK_KEY_Escape:
			find_select_exit(c);
			return TRUE;
		case GDK_KEY_e:
			find(c, &(Arg){.i = +1});
			evalscript(c,
					   "if(window._surfFindSelect)"
					   "_surfFindSelect(%d);",
					   t->find_current_match - 1);
			return TRUE;
		case GDK_KEY_V:
			evalscript(c,
					   "var s=window.getSelection();"
					   "s.modify('move','backward','lineboundary');"
					   "s.modify('extend','forward','lineboundary');");
			return TRUE;
		case GDK_KEY_w:
			evalscript(c, "window.getSelection()"
						  ".modify('extend','forward','word');");
			return TRUE;
		case GDK_KEY_b:
			evalscript(c, "window.getSelection()"
						  ".modify('extend','backward','word');");
			return TRUE;
		case GDK_KEY_y:
			webkit_web_view_evaluate_javascript(
				t->view, "window.getSelection().toString()",
				-1, NULL, NULL, NULL, find_select_yank_cb, c);
			return TRUE;
		}
		return TRUE;
	}

	for (i = 0; i < LENGTH(keys); ++i) {
		if (gdk_keyval_to_lower(keyval) == keys[i].keyval &&
			CLEANMASK(state) == keys[i].mod &&
			keys[i].func) {
			updateinstanceid(c);
			keys[i].func(c, &(keys[i].arg));
			return TRUE;
		}
	}
	return FALSE;
}

static void
winevent_enter(GtkEventControllerMotion *ctrl, double x, double y, Client *c)
{
	Tab *t = ctab(c);
	t->hover_link = (t->targeturi != NULL);
	updatetitle(c);
}

static void
winevent_leave(GtkEventControllerMotion *ctrl, Client *c)
{
	ctab(c)->hover_link = 0;
	updatetitle(c);
}

static void
winevent_fullscreen(GObject *win, GParamSpec *pspec, Client *c)
{
	c->fullscreen = gtk_window_is_fullscreen(GTK_WINDOW(win));
}

static void
showview(WebKitWebView *v, Client *c)
{
	GdkRGBA bgcolor = {0};
	GtkCssProvider *css;
	GtkEventController *barkey;
	char *cssstr;
	Tab *t = ctab(c);

	/* Bootstrap the first Tab's WebKit-derived fields now that the view
	 * is fully constructed and ready-to-show. */
	t->finder = webkit_web_view_get_find_controller(t->view);
	g_signal_connect(t->finder, "counted-matches",
	                 G_CALLBACK(findcountchanged), c);
	g_signal_connect(t->finder, "found-text",
	                 G_CALLBACK(foundtext), c);
	g_signal_connect(t->finder, "failed-to-find-text",
	                 G_CALLBACK(findfailed), c);
	t->pageid = webkit_web_view_get_page_id(t->view);

	c->inspector = webkit_web_view_get_inspector(t->view);
	c->win = createwindow(c);

	c->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	/* Tab bar at TOP */
	c->tabbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	gtk_widget_set_name(c->tabbar, "tabbar");
	gtk_box_append(GTK_BOX(c->vbox), c->tabbar);

	/* Download bar: below tabbar, hidden until a download starts */
	c->dlbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_name(c->dlbar, "surf-dlbar");
	gtk_box_append(GTK_BOX(c->vbox), c->dlbar);

	/* Webview expands to fill remaining space */
	gtk_widget_set_vexpand(GTK_WIDGET(t->view), TRUE);
	gtk_widget_set_hexpand(GTK_WIDGET(t->view), TRUE);
	gtk_box_append(GTK_BOX(c->vbox), GTK_WIDGET(t->view));

	/* Tab bar styling */
	GtkCssProvider *tabcss = gtk_css_provider_new();
	gchar *tabcssstr = g_strdup_printf(
		"#tabbar {"
		"  background-color: #1a1a1a;"
		"  padding: 0px;"
		"  font-family: 'Terminus (TTF)';"
		"  font-size: 13px;"
		"}"
		"#tab-active {"
		"  background-color: #4a4a4a;"
		"  color: #ffffff;"
		"  padding: 2px 8px;"
		"  margin: 0px;"
		"  border: none;"
		"  border-radius: 0px;"
		"}"
		"#tab-inactive {"
		"  background-color: #2a2a2a;"
		"  color: #888888;"
		"  padding: 2px 8px;"
		"  border-radius: 0px;"
		"}"
		"#tab-inactive:hover {"
		"  background-color: #353535;"
		"  color: #aaaaaa;"
		"}");
	gtk_css_provider_load_from_string(tabcss, tabcssstr);
	g_free(tabcssstr);

	gtk_style_context_add_provider_for_display(
		gdk_display_get_default(),
		GTK_STYLE_PROVIDER(tabcss),
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(tabcss);

	c->statusbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	c->barlabel = gtk_label_new("");
	gtk_widget_set_focusable(c->barlabel, FALSE);
	gtk_widget_set_name(c->barlabel, "surf-barlabel");
	gtk_widget_set_visible(c->barlabel, FALSE);
	gtk_box_append(GTK_BOX(c->statusbar), c->barlabel);

	c->statentry = gtk_entry_new();
	gtk_widget_set_hexpand(c->statentry, TRUE);
	gtk_widget_set_focusable(c->statentry, FALSE);
	gtk_editable_set_editable(GTK_EDITABLE(c->statentry), FALSE);
	g_signal_connect(G_OBJECT(c->statentry), "activate",
					 G_CALLBACK(baractivate), c);
	g_signal_connect(G_OBJECT(c->statentry), "changed",
					 G_CALLBACK(bar_on_changed), c);
	barkey = gtk_event_controller_key_new();
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(barkey),
											   GTK_PHASE_CAPTURE);
	g_signal_connect(barkey, "key-pressed", G_CALLBACK(barkeypress), c);
	gtk_widget_add_controller(c->statentry, barkey);
	gtk_box_append(GTK_BOX(c->statusbar), c->statentry);

	/* Status bar at BOTTOM */
	gtk_box_append(GTK_BOX(c->vbox), c->statusbar);

	css = gtk_css_provider_new();
	cssstr = g_strdup_printf(
		"#surf-statusbar {"
		"  background-color: %s;"
		"  padding: 1px 6px;"
		"  min-height: 18px;"
		"}"
		"#surf-barlabel {"
		"  background-color: %s;"
		"  color: %s;"
		"  font: %s;"
		"  padding: 1px 0 1px 6px;"
		"}"
		"#surf-statentry {"
		"  background-color: %s;"
		"  color: %s;"
		"  font: %s;"
		"  border: none;"
		"  border-radius: 0;"
		"  padding: 1px 6px;"
		"  min-height: 18px;"
		"}"
		"#surf-dlbar {"
		"  background-color: %s;"
		"  padding: 1px 6px;"
		"  min-height: 18px;"
		"}"
		"#surf-dllabel {"
		"  color: %s;"
		"  font: %s;"
		"  padding: 0 4px;"
		"}",
		stat_bg_normal,
		stat_bg_normal, stat_fg_normal, stat_font,
		stat_bg_normal, stat_fg_normal, stat_font,
		stat_bg_normal, stat_fg_normal, stat_font);
	gtk_css_provider_load_from_string(css, cssstr);
	g_free(cssstr);

	gtk_widget_set_name(c->statusbar, "surf-statusbar");
	gtk_widget_set_name(c->statentry, "surf-statentry");

	gtk_style_context_add_provider_for_display(
		gdk_display_get_default(),
		GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(css);

	gtk_window_set_child(GTK_WINDOW(c->win), c->vbox);

	gtk_widget_set_visible(c->dlbar, FALSE); /* hidden until first download */
	gtk_window_present(GTK_WINDOW(c->win));

	update_tabbar(c);
	gtk_widget_grab_focus(GTK_WIDGET(t->view));

	generate_instance_id(c);
	setup_fifo(c);
	updateinstanceid(c);
	if (showinstanceidflag) {
		gdk_display_sync(gtk_widget_get_display(c->win));
		puts(instanceidbuf);
		fflush(stdout);
	}

	if (curconfig[HideBackground].val.i)
		webkit_web_view_set_background_color(t->view, &bgcolor);

	if (curconfig[RunInFullscreen].val.i)
		togglefullscreen(c, NULL);

	if (curconfig[ZoomLevel].val.f != 1.0)
		webkit_web_view_set_zoom_level(t->view,
		                               curconfig[ZoomLevel].val.f);

	updatebar(c);
}

GtkWidget *
createwindow(Client *c)
{
	GtkWidget *w;
	GtkEventController *ctrl;

	w = gtk_window_new();
	gtk_window_set_default_size(GTK_WINDOW(w), winsize[0], winsize[1]);

	g_signal_connect(G_OBJECT(w), "destroy",
					 G_CALLBACK(destroywin), c);

	ctrl = gtk_event_controller_key_new();
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(ctrl),
											   GTK_PHASE_CAPTURE);
	g_signal_connect(ctrl, "key-pressed", G_CALLBACK(winevent_key), c);
	gtk_widget_add_controller(w, ctrl);

	ctrl = gtk_event_controller_motion_new();
	g_signal_connect(ctrl, "enter", G_CALLBACK(winevent_enter), c);
	g_signal_connect(ctrl, "leave", G_CALLBACK(winevent_leave), c);
	gtk_widget_add_controller(w, ctrl);

	g_signal_connect(G_OBJECT(w), "notify::fullscreened",
					 G_CALLBACK(winevent_fullscreen), c);

	return w;
}

/* Find the Tab index inside Client c that owns view v. Returns -1 if not found. */
static int
tab_index_for_view(Client *c, WebKitWebView *v)
{
	for (int i = 0; i < c->tabs_count; i++) {
		if (c->tabs[i].view == v)
			return i;
	}
	return -1;
}

static gboolean
loadfailedtls(WebKitWebView *v, gchar *uri, GTlsCertificate *cert,
			  GTlsCertificateFlags err, Client *c)
{
	GString *errmsg = g_string_new(NULL);
	gchar *html, *pem;
	int tidx = tab_index_for_view(c, v);

	if (tidx < 0)
		return FALSE;

	Tab *t = &c->tabs[tidx];
	t->failedcert = g_object_ref(cert);
	t->tlserr     = err;
	t->errorpage  = 1;

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
	                       "<p><pre>%s</pre></p>",
	                       uri, errmsg->str, pem);
	g_free(pem);
	g_string_free(errmsg, TRUE);

	webkit_web_view_load_alternate_html(t->view, html, uri, NULL);
	g_free(html);

	return TRUE;
}

static void
loadchanged(WebKitWebView *v, WebKitLoadEvent e, Client *c)
{
	const char *uri;
	Tab *t = g_object_get_data(G_OBJECT(v), "surf-tab");

	if (!t)
		return;

	uri = webkit_web_view_get_uri(v);
	if (!uri)
		return;

	gboolean is_active = (t == ctab(c));

	switch (e) {
	case WEBKIT_LOAD_STARTED:
		g_free(t->title);
		t->title = g_strdup(uri);
		t->https = t->insecure = 0;
		if (t->errorpage)
			t->errorpage = 0;
		else
			g_clear_object(&t->failedcert);

		if (is_active) {
			seturiparameters(c, uri, loadtransient);
			updatetitle(c);
		}
		update_tabbar(c);
		break;

	case WEBKIT_LOAD_REDIRECTED:
		g_free(t->title);
		t->title = g_strdup(uri);
		if (is_active) {
			seturiparameters(c, uri, loadtransient);
			updatetitle(c);
		}
		update_tabbar(c);
		break;

	case WEBKIT_LOAD_COMMITTED:
		g_free(t->title);
		t->title = g_strdup(uri);
		t->https = webkit_web_view_get_tls_info(t->view, &t->cert, &t->tlserr);
		if (is_active) {
			seturiparameters(c, uri, loadcommitted);
			updatetitle(c);
		}
		update_tabbar(c);
		break;

	case WEBKIT_LOAD_FINISHED:
		if (is_active) {
			seturiparameters(c, uri, loadfinished);
			runscript(c);
		}
		history_add(uri, t->title);
		update_tabbar(c);
		break;
	}
}

static void
progresschanged(WebKitWebView *v, GParamSpec *ps, Client *c)
{
	int tidx = tab_index_for_view(c, v);

	if (tidx < 0)
		return;

	Tab *t = &c->tabs[tidx];
	t->progress = webkit_web_view_get_estimated_load_progress(v) * 100;

	if (tidx == c->tabs_active)
		updatetitle(c);
	update_tabbar(c);
}

static void
titlechanged(WebKitWebView *view, GParamSpec *ps, Client *c)
{
	int tidx = tab_index_for_view(c, view);

	if (tidx < 0)
		return;

	Tab *t = &c->tabs[tidx];
	g_free(t->title);
	t->title = g_strdup(webkit_web_view_get_title(view));

	if (tidx == c->tabs_active)
		updatetitle(c);

	const char *uri = webkit_web_view_get_uri(view);
	if (t->title && *t->title && uri && !g_str_has_prefix(uri, "about:"))
		history_add(uri, t->title);

	update_tabbar(c);
}

static gboolean
viewusrmsgrcv(WebKitWebView *v, WebKitUserMessage *m, gpointer data)
{
	const char *name;
	WebKitUserMessage *r;
	Client *c;
	Client *hint = data;

	name = webkit_user_message_get_name(m);
	c = find_client_for_view(v, hint);

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

	/* Just acknowledge - scrolling uses evaluate_javascript now */
	r = webkit_user_message_new("surf-ack", NULL);
	webkit_user_message_send_reply(m, r);

	return TRUE;
}

static void
mousetargetchanged(WebKitWebView *v, WebKitHitTestResult *h, guint modifiers,
				   Client *c)
{
	WebKitHitTestResultContext hc = webkit_hit_test_result_get_context(h);
	int tidx = tab_index_for_view(c, v);

	if (tidx < 0)
		return;

	Tab *t = &c->tabs[tidx];

	if (t->mousepos)
		g_object_unref(t->mousepos);
	t->mousepos = g_object_ref(h);

	g_free(t->targeturi);
	if (hc & OnLink)
		t->targeturi = g_strdup(webkit_hit_test_result_get_link_uri(h));
	else if (hc & OnImg)
		t->targeturi = g_strdup(webkit_hit_test_result_get_image_uri(h));
	else if (hc & OnMedia)
		t->targeturi = g_strdup(webkit_hit_test_result_get_media_uri(h));
	else
		t->targeturi = NULL;

	t->hover_link = (t->targeturi != NULL);

	if (tidx == c->tabs_active)
		updatetitle(c);
}

static gboolean
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
	} else if (WEBKIT_IS_CLIPBOARD_PERMISSION_REQUEST(r)) {
		webkit_permission_request_allow(r);
		return TRUE;
	} else {
		return FALSE;
	}

	if (curconfig[param].val.i)
		webkit_permission_request_allow(r);
	else
		webkit_permission_request_deny(r);

	return TRUE;
}

static gboolean
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

static void
decidenavigation(WebKitPolicyDecision *d, Client *c)
{
	WebKitNavigationAction *a =
		webkit_navigation_policy_decision_get_navigation_action(
			WEBKIT_NAVIGATION_POLICY_DECISION(d));

	switch (webkit_navigation_action_get_navigation_type(a)) {
	case WEBKIT_NAVIGATION_TYPE_LINK_CLICKED:
	case WEBKIT_NAVIGATION_TYPE_FORM_SUBMITTED:
	case WEBKIT_NAVIGATION_TYPE_BACK_FORWARD:
	case WEBKIT_NAVIGATION_TYPE_RELOAD:
	case WEBKIT_NAVIGATION_TYPE_FORM_RESUBMITTED:
	case WEBKIT_NAVIGATION_TYPE_OTHER:
	default:
		if (webkit_navigation_action_get_frame_name(a)) {
			webkit_policy_decision_ignore(d);
		} else {
			webkit_policy_decision_use(d);
		}
		break;
	}
}

static void
decidenewwindow(WebKitPolicyDecision *d, Client *c)
{
	Arg arg;
	int prev;
	WebKitNavigationAction *a =
		webkit_navigation_policy_decision_get_navigation_action(
			WEBKIT_NAVIGATION_POLICY_DECISION(d));

	switch (webkit_navigation_action_get_navigation_type(a)) {
	case WEBKIT_NAVIGATION_TYPE_LINK_CLICKED:
	case WEBKIT_NAVIGATION_TYPE_FORM_SUBMITTED:
	case WEBKIT_NAVIGATION_TYPE_BACK_FORWARD:
	case WEBKIT_NAVIGATION_TYPE_RELOAD:
	case WEBKIT_NAVIGATION_TYPE_FORM_RESUBMITTED:
		arg.v = webkit_uri_request_get_uri(
			webkit_navigation_action_get_request(a));
		prev = c->tabs_active;
		tab_new(c, &(Arg){.i = 1});
		loaduri(c, &arg);
		tab_switch_to(c, prev);
		break;
	case WEBKIT_NAVIGATION_TYPE_OTHER:
	default:
		break;
	}

	webkit_policy_decision_ignore(d);
}

static void
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

	if (!g_str_has_prefix(uri, "http://") && !g_str_has_prefix(uri, "https://") && !g_str_has_prefix(uri, "about:") && !g_str_has_prefix(uri, "file://") && !g_str_has_prefix(uri, "webkit://") && !g_str_has_prefix(uri, "data:") && !g_str_has_prefix(uri, "blob:") && !(g_str_has_prefix(uri, "webkit-pdfjs-viewer://") && curconfig[PDFJSviewer].val.i) && strlen(uri) > 0) {
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
		webkit_policy_decision_download(d);
	}
}

static void
insecurecontent(WebKitWebView *v, WebKitInsecureContentEvent e, Client *c)
{
	int tidx = tab_index_for_view(c, v);
	if (tidx >= 0)
		c->tabs[tidx].insecure = 1;
}

static gchar *
fmt_size(guint64 bytes)
{
	if (bytes >= (guint64)1024 * 1024 * 1024)
		return g_strdup_printf("%.2fGB", bytes / (1024.0 * 1024.0 * 1024.0));
	if (bytes >= 1024 * 1024)
		return g_strdup_printf("%.2fMB", bytes / (1024.0 * 1024.0));
	if (bytes >= 1024)
		return g_strdup_printf("%.2fKB", bytes / 1024.0);
	return g_strdup_printf("%" G_GUINT64_FORMAT "B", bytes);
}

static void
dl_update_label(WebKitDownload *d)
{
	DlData *dd = g_object_get_data(G_OBJECT(d), "dl-data");
	if (!dd || !dd->label)
		return;

	gdouble elapsed = webkit_download_get_elapsed_time(d);
	guint64 recv = webkit_download_get_received_data_length(d);
	gdouble pct = webkit_download_get_estimated_progress(d);

	/* Speed: exponential moving average, sampled when enough time passed */
	gdouble dt = elapsed - dd->prev_time;
	if (dt >= 0.5 && !dd->done) {
		gdouble inst = (recv - dd->prev_recv) / dt;
		dd->speed = (dd->prev_time == 0.0) ? inst : dd->speed * 0.6 + inst * 0.4;
		dd->prev_recv = recv;
		dd->prev_time = elapsed;
	}

	gchar *recv_str = fmt_size(recv);
	int esec = (int)elapsed;
	gchar *txt;

	const gchar *name = dd->name ? dd->name : "?";
	if (dd->done) {
		if (dd->total > 0) {
			gchar *tot_str = fmt_size((guint64)dd->total);
			txt = g_strdup_printf("%u: %s [100%%|%s]", dd->index, name, tot_str);
			g_free(tot_str);
		} else {
			txt = g_strdup_printf("%u: %s [done|%s]", dd->index, name, recv_str);
		}
	} else if (dd->speed > 0 && dd->total > 0) {
		gchar *spd_str = fmt_size((guint64)dd->speed);
		gchar *tot_str = fmt_size((guint64)dd->total);
		txt = g_strdup_printf("%u: %s [%s/s|%d:%02d|%.0f%%|%s/%s]",
							  dd->index, name, spd_str, esec / 60, esec % 60,
							  pct * 100.0, recv_str, tot_str);
		g_free(spd_str);
		g_free(tot_str);
	} else if (dd->total > 0) {
		gchar *tot_str = fmt_size((guint64)dd->total);
		txt = g_strdup_printf("%u: %s [%d:%02d|%.0f%%|%s/%s]",
							  dd->index, name, esec / 60, esec % 60,
							  pct * 100.0, recv_str, tot_str);
		g_free(tot_str);
	} else {
		txt = g_strdup_printf("%u: %s [%s]", dd->index, name, recv_str);
	}
	g_free(recv_str);
	gtk_label_set_text(GTK_LABEL(dd->label), txt);
	g_free(txt);
}

static void
downloadstarted(WebKitNetworkSession *ns, WebKitDownload *d, Client *c)
{
	g_signal_connect(G_OBJECT(d), "decide-destination",
					 G_CALLBACK(decidedestination), c);
}

static void
dl_data_free(DlData *dd)
{
	g_free(dd->name);
	g_free(dd);
}

static gboolean
decidedestination(WebKitDownload *d, gchar *suggested_filename, Client *c)
{
	/* If user already confirmed a path, use it directly for this restarted download */
	if (c->dl_pending_path) {
		webkit_download_set_destination(d, c->dl_pending_path);

		static guint dl_counter = 0;
		WebKitURIResponse *resp = webkit_download_get_response(d);
		gint64 clen = resp ? webkit_uri_response_get_content_length(resp) : -1;

		DlData *dd = g_new0(DlData, 1);
		dd->index = ++dl_counter;
		dd->name = g_path_get_basename(c->dl_pending_path);
		dd->total = clen;
		dd->done = FALSE;

		GtkWidget *lbl = gtk_label_new(NULL);
		gtk_widget_set_name(lbl, "surf-dllabel");
		dd->label = lbl;
		g_object_set_data_full(G_OBJECT(d), "dl-data", dd,
							   (GDestroyNotify)dl_data_free);
		gtk_box_append(GTK_BOX(c->dlbar), lbl);
		dl_update_label(d);
		gtk_widget_set_visible(c->dlbar, TRUE);

		g_signal_connect(d, "notify::estimated-progress",
						 G_CALLBACK(dlprogress), NULL);
		g_signal_connect(d, "finished", G_CALLBACK(dlfinished), NULL);
		g_signal_connect(d, "failed", G_CALLBACK(dlfailed), NULL);

		g_free(c->dl_pending_path);
		c->dl_pending_path = NULL;
		return TRUE;
	}

	/* First time: cancel, prompt for path, restart after confirm */
	WebKitURIRequest *req = webkit_download_get_request(d);
	g_free(c->dl_pending_uri);
	c->dl_pending_uri = g_strdup(webkit_uri_request_get_uri(req));

	const char *dldir = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
	if (!dldir)
		dldir = g_get_home_dir();
	gchar *default_dest = g_build_filename(dldir, suggested_filename, NULL);

	ctab(c)->mode = ModeCommand;
	gtk_label_set_text(GTK_LABEL(c->barlabel), " [DOWNLOAD] ");
	gtk_widget_set_visible(c->barlabel, TRUE);
	gtk_widget_set_focusable(c->statentry, TRUE);
	gtk_editable_set_editable(GTK_EDITABLE(c->statentry), TRUE);
	gtk_editable_set_text(GTK_EDITABLE(c->statentry), default_dest);
	gtk_widget_grab_focus(c->statentry);
	gtk_editable_set_position(GTK_EDITABLE(c->statentry), -1);
	updatebar_style(c);
	g_free(default_dest);

	webkit_download_cancel(d);
	return TRUE;
}

static void
dlprogress(WebKitDownload *d, GParamSpec *ps, gpointer unused)
{
	dl_update_label(d);
}

static void
dlfinished(WebKitDownload *d, gpointer unused)
{
	DlData *dd = g_object_get_data(G_OBJECT(d), "dl-data");
	if (!dd)
		return;
	dd->done = TRUE;
	dl_update_label(d);
}

static void
dlfailed(WebKitDownload *d, GError *err, gpointer unused)
{
	(void)err;
	DlData *dd = g_object_get_data(G_OBJECT(d), "dl-data");
	if (!dd)
		return;
	dd->done = TRUE;
	dl_update_label(d);
}

static void
dl_clear(Client *c, const Arg *a)
{
	GtkWidget *child;
	while ((child = gtk_widget_get_first_child(c->dlbar)))
		gtk_box_remove(GTK_BOX(c->dlbar), child);
	gtk_widget_set_visible(c->dlbar, FALSE);
}

static void
webprocessterminated(WebKitWebView *v, WebKitWebProcessTerminationReason r,
					 Client *c)
{
	fprintf(stderr, "web process terminated: %s\n",
			r == WEBKIT_WEB_PROCESS_CRASHED ? "crashed" : "no memory");
	webkit_web_view_reload(v);
}

typedef struct {
	WebKitFileChooserRequest *req;
	gboolean allow_multiple;
	char *tmpfile;
} FileChooserData;

static void
filechooser_done(GObject *source, GAsyncResult *result, gpointer userdata)
{
	FileChooserData *fcd = userdata;
	GError *err = NULL;

	g_subprocess_wait_finish(G_SUBPROCESS(source), result, &err);
	if (err) {
		g_error_free(err);
		webkit_file_chooser_request_cancel(fcd->req);
		goto cleanup;
	}

	/* Read selected paths from the temp file, one per line */
	gchar *contents = NULL;
	if (!g_file_get_contents(fcd->tmpfile, &contents, NULL, NULL) ||
		!contents || *contents == '\0') {
		webkit_file_chooser_request_cancel(fcd->req);
		goto cleanup;
	}

	gchar **lines = g_strsplit(contents, "\n", -1);
	g_free(contents);

	/* Build NULL-terminated array of non-empty paths */
	GPtrArray *paths = g_ptr_array_new();
	for (int i = 0; lines[i]; i++) {
		if (lines[i][0] != '\0')
			g_ptr_array_add(paths, lines[i]);
	}
	g_ptr_array_add(paths, NULL);

	if (paths->len > 1) {
		webkit_file_chooser_request_select_files(fcd->req,
												 (const gchar *const *)paths->pdata);
	} else {
		webkit_file_chooser_request_cancel(fcd->req);
	}

	g_strfreev(lines);
	g_ptr_array_free(paths, FALSE);

cleanup:
	g_unlink(fcd->tmpfile);
	g_free(fcd->tmpfile);
	g_object_unref(fcd->req);
	g_free(fcd);
}

static gboolean
filechooser(WebKitWebView *v, WebKitFileChooserRequest *r, Client *c)
{
	(void)v;
	(void)c;

	if (!filepicker_cmd[0])
		return FALSE; /* fall back to default dialog */

	/* Create a temp file for the picker to write selected paths to */
	gchar *tmpfile = g_strdup("/tmp/surf-filepick-XXXXXX");
	int fd = mkstemp(tmpfile);
	if (fd < 0) {
		g_free(tmpfile);
		return FALSE;
	}
	close(fd);

	/* Build argv: replace {} with the temp file path */
	GPtrArray *argv = g_ptr_array_new();
	for (int i = 0; filepicker_cmd[i]; i++) {
		if (strcmp(filepicker_cmd[i], "{}") == 0)
			g_ptr_array_add(argv, tmpfile);
		else {
			/* Also replace {} inside shell -c string */
			if (strstr(filepicker_cmd[i], "{}")) {
				gchar **parts = g_strsplit(filepicker_cmd[i], "{}", -1);
				gchar *replaced = g_strjoinv(tmpfile, parts);
				g_strfreev(parts);
				g_ptr_array_add(argv, replaced);
				/* replaced is owned by argv; free after subprocess */
			} else {
				g_ptr_array_add(argv, (gpointer)filepicker_cmd[i]);
			}
		}
	}
	g_ptr_array_add(argv, NULL);

	GError *err = NULL;
	GSubprocess *proc = g_subprocess_newv(
		(const gchar *const *)argv->pdata,
		G_SUBPROCESS_FLAGS_NONE, &err);

	/* Free any replaced strings */
	for (guint i = 0; i < argv->len - 1; i++) {
		if (argv->pdata[i] != tmpfile &&
			(filepicker_cmd[i] == NULL ||
			 (argv->pdata[i] != (gpointer)filepicker_cmd[i])))
			g_free(argv->pdata[i]);
	}
	g_ptr_array_free(argv, FALSE);

	if (!proc || err) {
		if (err)
			g_error_free(err);
		g_unlink(tmpfile);
		g_free(tmpfile);
		return FALSE;
	}

	FileChooserData *fcd = g_new0(FileChooserData, 1);
	fcd->req = g_object_ref(r);
	fcd->allow_multiple = webkit_file_chooser_request_get_select_multiple(r);
	fcd->tmpfile = tmpfile;

	g_subprocess_wait_async(proc, NULL, filechooser_done, fcd);
	g_object_unref(proc);

	return TRUE; /* handled */
}

static void
closeview(WebKitWebView *v, Client *c)
{
	if (c->tabs_count > 1) {
		for (int i = 0; i < c->tabs_count; i++) {
			if (c->tabs[i].view == v) {
				if (i != c->tabs_active)
					tab_switch_to(c, i);
				tab_close(c, &(Arg){0});
				return;
			}
		}
	}
	gtk_window_destroy(GTK_WINDOW(c->win));
}

static void
destroywin(GtkWidget *w, Client *c)
{
	destroyclient(c);
	if (!clients)
		g_main_loop_quit(mainloop);
}

static void
pasteuri(GObject *clipboard, GAsyncResult *result, gpointer d)
{
	char *text = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(clipboard),
												result, NULL);
	Arg a = {.v = text};
	if (text) {
		loaduri((Client *)d, &a);
		g_free(text);
	}
}

static void
reload(Client *c, const Arg *a)
{
	WebKitWebView *v = ctab(c)->view;
	if (a->i)
		webkit_web_view_reload_bypass_cache(v);
	else
		webkit_web_view_reload(v);
}

static void
print(Client *c, const Arg *a)
{
	(void)a;
	webkit_print_operation_run_dialog(webkit_print_operation_new(ctab(c)->view),
	                                  GTK_WINDOW(c->win));
}

static void
screenshot_cb(GObject *obj, GAsyncResult *res, gpointer data)
{
	Client *c = data;
	GdkTexture *texture;
	GError *err = NULL;
	char path[PATH_MAX];
	char *home;
	struct tm *tm;
	time_t t;

	/* Validate that the view still exists in this client */
	if (tab_index_for_view(c, WEBKIT_WEB_VIEW(obj)) < 0)
		return;

	texture = webkit_web_view_get_snapshot_finish(WEBKIT_WEB_VIEW(obj),
													  res, &err);
	if (err) {
		fprintf(stderr, "screenshot: %s\n", err->message);
		g_error_free(err);
		return;
	}

	home = getenv("HOME");
	if (!home)
		home = "/tmp";

	t = time(NULL);
	tm = localtime(&t);

	snprintf(path, sizeof(path), "%s/Pictures", home);
	if (access(path, W_OK) != 0)
		snprintf(path, sizeof(path), "%s", home);

	snprintf(path + strlen(path), sizeof(path) - strlen(path),
			 "/surf-%04d%02d%02d-%02d%02d%02d.png",
			 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
			 tm->tm_hour, tm->tm_min, tm->tm_sec);

	if (!gdk_texture_save_to_png(texture, path))
		fprintf(stderr, "screenshot: failed to save %s\n", path);
	else
		fprintf(stderr, "screenshot: %s\n", path);
	g_object_unref(texture);
}


static void
screenshot(Client *c, const Arg *a)
{
	(void)a;
	webkit_web_view_get_snapshot(ctab(c)->view,
	                             WEBKIT_SNAPSHOT_REGION_FULL_DOCUMENT,
	                             WEBKIT_SNAPSHOT_OPTIONS_NONE,
	                             NULL, screenshot_cb, c);
}

static void
showcert(Client *c, const Arg *a)
{
	(void)a;
	Tab *t = ctab(c);
	GTlsCertificate *cert = t->failedcert ? t->failedcert : t->cert;
	GtkWidget *win, *scroll, *view;
	GtkTextBuffer *buf;
	gchar *pem = NULL;

	if (!cert)
		return;

	g_object_get(cert, "certificate-pem", &pem, NULL);
	if (!pem)
		return;

	win = gtk_window_new();
	gtk_window_set_default_size(GTK_WINDOW(win), 600, 400);
	gtk_window_set_title(GTK_WINDOW(win), "Certificate");

	scroll = gtk_scrolled_window_new();
	view = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
	buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
	gtk_text_buffer_set_text(buf, pem, -1);
	g_free(pem);

	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), view);
	gtk_window_set_child(GTK_WINDOW(win), scroll);
	gtk_window_present(GTK_WINDOW(win));
}

static void
clipboard(Client *c, const Arg *a)
{
	GdkDisplay *dpy = gdk_display_get_default();

	if (a->i) {
		gdk_clipboard_read_text_async(gdk_display_get_primary_clipboard(dpy),
									  NULL, pasteuri, c);
	} else {
		const char *uri = ctab(c)->targeturi ? ctab(c)->targeturi : geturi(c);
		gdk_clipboard_set_text(gdk_display_get_clipboard(dpy), uri);
		gdk_clipboard_set_text(gdk_display_get_primary_clipboard(dpy), uri);
	}
}

static void
zoom(Client *c, const Arg *a)
{
	WebKitWebView *v = ctab(c)->view;
	if (a->i > 0)
		webkit_web_view_set_zoom_level(v, curconfig[ZoomLevel].val.f + 0.1);
	else if (a->i < 0)
		webkit_web_view_set_zoom_level(v, curconfig[ZoomLevel].val.f - 0.1);
	else
		webkit_web_view_set_zoom_level(v, 1.0);

	curconfig[ZoomLevel].val.f = webkit_web_view_get_zoom_level(v);
}

static void
scrollv(Client *c, const Arg *a)
{
	char js[128];
	snprintf(js, sizeof(js),
			 "document.scrollingElement.scrollTop"
			 "+=window.innerHeight/100*%d;", a->i);
	webkit_web_view_evaluate_javascript(ctab(c)->view, js, -1,
										NULL, NULL, NULL, NULL, NULL);
}

static void
scrollh(Client *c, const Arg *a)
{
	char js[128];
	snprintf(js, sizeof(js),
			 "window.scrollBy(window.innerWidth/100*%d,0);", a->i);
	webkit_web_view_evaluate_javascript(ctab(c)->view, js, -1,
										NULL, NULL, NULL, NULL, NULL);
}

static void
navigate(Client *c, const Arg *a)
{
	WebKitWebView *v = ctab(c)->view;
	if (a->i < 0)
		webkit_web_view_go_back(v);
	else if (a->i > 0)
		webkit_web_view_go_forward(v);
}

static void
reloaduserscripts(Client *c, const Arg *a)
{
	(void)a;

	if (!shared_content_manager)
		return;

	webkit_user_content_manager_remove_all_scripts(shared_content_manager);
	inject_userscripts_early(shared_content_manager, "");

	for (Client *it = clients; it; it = it->next) {
		for (int i = 0; i < it->tabs_count; i++) {
			if (it->tabs[i].view)
				webkit_web_view_reload(it->tabs[i].view);
		}
	}
}

static void
stop(Client *c, const Arg *a)
{
	(void)a;
	webkit_web_view_stop_loading(ctab(c)->view);
}

static void
toggle(Client *c, const Arg *a)
{
	curconfig[a->i].val.i ^= 1;
	setparameter(c, 1, (ParamName)a->i, &curconfig[a->i].val);
}

static void
togglefullscreen(Client *c, const Arg *a)
{
	if (c->fullscreen)
		gtk_window_unfullscreen(GTK_WINDOW(c->win));
	else
		gtk_window_fullscreen(GTK_WINDOW(c->win));
}

static void
togglecookiepolicy(Client *c, const Arg *a)
{
	++cookiepolicy;
	cookiepolicy %= strlen(curconfig[CookiePolicies].val.v);

	setparameter(c, 0, CookiePolicies, NULL);
}

static void
toggleinspector(Client *c, const Arg *a)
{
	WebKitWebInspector *inspector;

	inspector = webkit_web_view_get_inspector(ctab(c)->view);
	c->inspector = inspector;

	if (webkit_web_inspector_get_web_view(inspector)) {
		webkit_web_inspector_close(inspector);
	} else if (curconfig[Inspector].val.i) {
		if (webkit_web_inspector_get_can_attach(inspector))
			webkit_web_inspector_attach(inspector);
		webkit_web_inspector_show(inspector);
	}
}

static void
find(Client *c, const Arg *a)
{
	Tab *t = ctab(c);
	if (a && a->i) {
		if (a->i > 0) {
			webkit_find_controller_search_next(t->finder);
			/* We rely on WebKit's internal match tracking - found-text signal
			 * updates find_current_match, and counted-matches updates find_match_count.
			 * No manual bookkeeping needed. */
		} else {
			webkit_find_controller_search_previous(t->finder);
			/* We rely on WebKit's internal match tracking - found-text signal
			 * updates find_current_match, and counted-matches updates find_match_count.
			 * No manual bookkeeping needed. */
		}
		updatebar(c);
	} else {
		opensearch(c, NULL);
	}
}

static void
opensearch(Client *c, const Arg *a)
{
	(void)a;
	Tab *t = ctab(c);
	t->mode = ModeSearch;
	t->find_match_count = 0;
	t->find_current_match = 0;

	gtk_label_set_text(GTK_LABEL(c->barlabel), " [SEARCH] ");
	gtk_widget_set_visible(c->barlabel, TRUE);

	gtk_widget_set_focusable(c->statentry, TRUE);
	gtk_editable_set_editable(GTK_EDITABLE(c->statentry), TRUE);
	gtk_editable_set_text(GTK_EDITABLE(c->statentry), "");
	gtk_widget_grab_focus(c->statentry);
	gtk_editable_set_position(GTK_EDITABLE(c->statentry), -1);
	updatebar_style(c);
	updatetitle(c);
}

static void
bar_on_changed(GtkEditable *editable, Client *c)
{
	if (ctab(c)->mode == ModeCommand)
		history_filter(c, gtk_editable_get_text(editable));
}

static gboolean
bar_update_search(gpointer data)
{
	/* Do nothing - search only happens on Enter */
	return FALSE;
}

static void
updatebar(Client *c)
{
	char *text;
	const char *uri, *modestr, *priv;
	Tab *t = ctab(c);

	if (!c->statentry)
		return;

	if (t->mode == ModeCommand || t->mode == ModeSearch)
		return;

	updatebar_style(c);
	uri = geturi(c);

	switch (t->mode) {
	case ModeInsert:
		modestr = "INSERT";
		break;
	case ModeHint:
		modestr = "HINT";
		break;
	case ModeCommand:
		modestr = "TAB";
		break;
	case ModeSearch:
		modestr = "SEARCH";
		break;
	case ModeSelect:
		modestr = "SELECT";
		break;
	default:
		modestr = "NORMAL";
		break;
	}
	priv = curconfig[PrivacyMode].val.i ? "|PRIV" : "";

	if (t->progress > 0 && t->progress < 100)
		text = g_strdup_printf(" [%s%s] [%i%%] %s", modestr, priv,
		                       t->progress, uri);
	else if (t->find_match_count > 0)
		text = g_strdup_printf(" [%s%s] [%d/%d] %s", modestr, priv,
		                       t->find_current_match, t->find_match_count, uri);
	else
		text = g_strdup_printf(" [%s%s] %s", modestr, priv, uri);

	gtk_editable_set_text(GTK_EDITABLE(c->statentry), text);
	g_free(text);
}

static void
openbar(Client *c, const Arg *a)
{
	const char *uri;

	ctab(c)->mode = ModeCommand;

	history_load();

	gtk_label_set_text(GTK_LABEL(c->barlabel), " [TAB] ");
	gtk_widget_set_visible(c->barlabel, TRUE);

	gtk_widget_set_focusable(c->statentry, TRUE);
	gtk_editable_set_editable(GTK_EDITABLE(c->statentry), TRUE);

	if (a->i) {
		uri = geturi(c);
		gtk_editable_set_text(GTK_EDITABLE(c->statentry), uri);
	} else {
		gtk_editable_set_text(GTK_EDITABLE(c->statentry), "");
	}

	gtk_widget_grab_focus(c->statentry);
	gtk_editable_set_position(GTK_EDITABLE(c->statentry), -1);

	history_filter(c, "");
	updatebar_style(c);
	updatetitle(c);
}

static void
closebar(Client *c)
{
	ctab(c)->mode = ModeNormal;
	c->newtab_pending = 0;

	history_hide(c);

	gtk_widget_set_visible(c->barlabel, FALSE);
	gtk_label_set_text(GTK_LABEL(c->barlabel), "");

	gtk_editable_set_editable(GTK_EDITABLE(c->statentry), FALSE);
	gtk_widget_set_focusable(c->statentry, FALSE);

	gtk_widget_grab_focus(GTK_WIDGET(ctab(c)->view));
	updatetitle(c);
	updatebar(c);
}

static void
openbar_newtab(Client *c, const Arg *a)
{
	(void)a;
	ctab(c)->mode = ModeCommand;
	c->newtab_pending = 1;

	history_load();

	gtk_label_set_text(GTK_LABEL(c->barlabel), " [NEW TAB] ");
	gtk_widget_set_visible(c->barlabel, TRUE);

	gtk_widget_set_focusable(c->statentry, TRUE);
	gtk_editable_set_editable(GTK_EDITABLE(c->statentry), TRUE);

	gtk_editable_set_text(GTK_EDITABLE(c->statentry), "");

	gtk_widget_grab_focus(c->statentry);
	gtk_editable_set_position(GTK_EDITABLE(c->statentry), -1);

	history_filter(c, "");
	updatebar_style(c);
}

static void
find_highlight_update(Client *c, const char *needle)
{
	if (!needle || !*needle) {
		evalscript(c,
				   "window._surfFindRanges=[];"
				   "if(typeof CSS!=='undefined'&&CSS.highlights){"
				   "CSS.highlights.delete('surf-find');"
				   "CSS.highlights.delete('surf-find-sel');}");
		return;
	}
	gchar *esc = g_strescape(needle, NULL);
	evalscript(
		c,
		"(function(){"
		"if(!document.getElementById('_surf_find_css')){"
		"var s=document.createElement('style');"
		"s.id='_surf_find_css';"
		"s.textContent="
		"'::highlight(surf-find){background:#ff8c00;color:#000;}'"
		"+'::highlight(surf-find-sel){background:#fff700;color:#000;"
		"outline:2px solid #ff6600;}';"
		"(document.head||document.documentElement).appendChild(s);}"
		"if(typeof CSS==='undefined'||!CSS.highlights)return;"
		"CSS.highlights.delete('surf-find');"
		"CSS.highlights.delete('surf-find-sel');"
		"var nd='%s',lo=nd.toLowerCase(),len=nd.length;"
		"var w=document.createTreeWalker(document.body,NodeFilter.SHOW_TEXT,null);"
		"var rs=[],n,t,lt,i;"
		"while((n=w.nextNode())){"
		"t=n.textContent;lt=t.toLowerCase();i=0;"
		"while((i=lt.indexOf(lo,i))!==-1){"
		"var r=new Range();r.setStart(n,i);r.setEnd(n,i+len);"
		"rs.push(r);i+=len;}}"
		"window._surfFindRanges=rs;"
		"window._surfFindSelect=function(idx){"
		"var sel=window.getSelection();sel.removeAllRanges();"
		"CSS.highlights.delete('surf-find-sel');"
		"if(rs[idx]){"
		"sel.addRange(rs[idx]);"
		"CSS.highlights.set('surf-find-sel',new Highlight(rs[idx]));}"
		"};"
		"if(rs.length)CSS.highlights.set('surf-find',new Highlight(...rs));"
		"})()",
		esc);
	g_free(esc);
}

static void
find_select_exit(Client *c)
{
	evalscript(c,
			   "window.getSelection().removeAllRanges();"
			   "if(typeof CSS!=='undefined'&&CSS.highlights)"
			   "CSS.highlights.delete('surf-find-sel');");
	ctab(c)->mode = ModeNormal;
	updatebar(c);
	updatebar_style(c);
}

static void
find_select_enter(Client *c, const Arg *a)
{
	(void)a;
	Tab *t = ctab(c);
	if (t->find_match_count <= 0)
		return;
	t->mode = ModeSelect;
	evalscript(c, "if(window._surfFindSelect)_surfFindSelect(%d);",
			   t->find_current_match - 1);
	updatebar(c);
	updatebar_style(c);
}

static void
find_select_line(Client *c, const Arg *a)
{
	(void)a;
	Tab *t = ctab(c);
	if (t->find_match_count <= 0)
		return;
	t->mode = ModeSelect;
	evalscript(c,
			   "if(window._surfFindSelect){"
			   "_surfFindSelect(%d);"
			   "var s=window.getSelection();"
			   "s.modify('move','backward','lineboundary');"
			   "s.modify('extend','forward','lineboundary');}",
			   t->find_current_match - 1);
	updatebar(c);
	updatebar_style(c);
}

static void
find_select_yank_cb(GObject *obj, GAsyncResult *res, gpointer data)
{
	Client *c = data;
	JSCValue *val;
	GError *err = NULL;
	char *text;

	/* Validate that the view still exists in this client */
	if (tab_index_for_view(c, WEBKIT_WEB_VIEW(obj)) < 0)
		return;

	val = webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(obj),
													 res, &err);
	if (err) {
		g_error_free(err);
		return;
	}
	if (!val || !jsc_value_is_string(val)) {
		if (val)
			g_object_unref(val);
		return;
	}
	text = jsc_value_to_string(val);
	g_object_unref(val);

	if (text && *text) {
		GdkDisplay *dpy = gdk_display_get_default();
		gdk_clipboard_set_text(gdk_display_get_clipboard(dpy), text);
		gdk_clipboard_set_text(gdk_display_get_primary_clipboard(dpy), text);
	}
	g_free(text);
	find_select_exit(c);
}

static void
baractivate(GtkEntry *entry, Client *c)
{
	const char *text, *input;
	Tab *t = ctab(c);

	text = gtk_editable_get_text(GTK_EDITABLE(entry));

	if (t->mode == ModeSearch) {
		input = text;

		if (input && *input) {
			webkit_find_controller_search(t->finder, input,
										  findopts, G_MAXUINT);
			webkit_find_controller_count_matches(t->finder, input,
												 findopts, G_MAXUINT);
			find_highlight_update(c, input);
			t->find_current_match = 1;
		}

		closebar(c);
		updatebar(c);
		return;
	}

	/* Download path confirmation */
	if (c->dl_pending_uri) {
		const char *path = gtk_editable_get_text(GTK_EDITABLE(entry));
		if (path && *path) {
			gchar *dir = g_path_get_dirname(path);
			g_mkdir_with_parents(dir, 0755);
			g_free(dir);
			c->dl_pending_path = g_strdup(path);
			webkit_web_view_download_uri(t->view, c->dl_pending_uri);
		}
		g_free(c->dl_pending_uri);
		c->dl_pending_uri = NULL;
		closebar(c);
		return;
	}

	/* Command mode */
	input = text;

	history_hide(c);

	if (input && *input) {
		if (c->newtab_pending) {
			gchar *saved_input = g_strdup(input);
			c->newtab_pending = 0;

			closebar(c);

			tab_new(c, &(Arg){.i = 1});
			Arg a = {.v = saved_input};
			loaduri(c, &a);
			g_free(saved_input);
			return;
		} else {
			Arg a = {.v = input};
			loaduri(c, &a);
		}
	} else {
		c->newtab_pending = 0;
	}

	closebar(c);
}

static gboolean
barkeypress(GtkEventControllerKey *ctrl, guint keyval, guint keycode,
			GdkModifierType state, Client *c)
{
	Tab *t = ctab(c);

	if (keyval == GDK_KEY_Escape) {
		if (t->mode == ModeSearch) {
			webkit_find_controller_search_finish(t->finder);
			find_highlight_update(c, NULL);
		}
		if (c->dl_pending_uri) {
			g_free(c->dl_pending_uri);
			c->dl_pending_uri = NULL;
		}
		closebar(c);
		return TRUE;
	}

	if (t->mode == ModeSearch) {
		g_idle_add((GSourceFunc)bar_update_search, c);
		return FALSE;
	}

	if (c->dl_pending_uri)
		return FALSE;

	if (keyval == GDK_KEY_Up) {
		history_select(c, -1);
		return TRUE;
	}
	if (keyval == GDK_KEY_Down) {
		history_select(c, +1);
		return TRUE;
	}
	if (keyval == GDK_KEY_Tab) {
		if (state & GDK_SHIFT_MASK)
			history_select(c, -1);
		else
			history_select(c, +1);
		return TRUE;
	}
	if ((state & GDK_CONTROL_MASK) && keyval == GDK_KEY_n) {
		history_select(c, +1);
		return TRUE;
	}
	if ((state & GDK_CONTROL_MASK) && keyval == GDK_KEY_p) {
		history_select(c, -1);
		return TRUE;
	}

	return FALSE;
}

static void
toggleinsert(Client *c, const Arg *a)
{
	(void)a;
	Tab *t = ctab(c);
	if (t->mode == ModeInsert)
		t->mode = ModeNormal;
	else
		t->mode = ModeInsert;
	updatetitle(c);
}

static void
clicknavigate(Client *c, const Arg *a, WebKitHitTestResult *h)
{
	navigate(c, a);
}

static void
clicknewwindow(Client *c, const Arg *a, WebKitHitTestResult *h)
{
	Arg arg;

	arg.v = webkit_hit_test_result_get_link_uri(h);
	newwindow(c, &arg, a->i);
}

static void
clicknewtab(Client *c, const Arg *a, WebKitHitTestResult *h)
{
	int prev = c->tabs_active;
	Arg arg;

	(void)a;
	arg.v = webkit_hit_test_result_get_link_uri(h);
	tab_new(c, &(Arg){.i = 1});
	loaduri(c, &arg);
	tab_switch_to(c, prev);
}

static void
clickexternplayer(Client *c, const Arg *a, WebKitHitTestResult *h)
{
	Arg arg;

	arg = (Arg)VIDEOPLAY(webkit_hit_test_result_get_media_uri(h));
	spawn(c, &arg);
}

static gboolean
fifo_read(GIOChannel *chan, GIOCondition cond, gpointer data)
{
	Client *c = data;
	gchar *line = NULL;
	gsize len;
	GError *err = NULL;

	if (!c)
		return TRUE;

	if (g_io_channel_read_line(chan, &line, &len, NULL, &err) == G_IO_STATUS_NORMAL) {
		if (line) {
			g_strstrip(line);

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
					Arg a = {.v = url};
					if (new_tab) {
						tab_new(c, &(Arg){.i = 1});
						loaduri(c, &a);
					} else {
						loaduri(c, &a);
					}
				}
			}
			g_free(line);
		}
	}

	if (err)
		g_error_free(err);
	return TRUE;
}

static void
spawnuserscript(Client *c, const Arg *a)
{
	const char *script = a->v;
	const char *uri = geturi(c);
	const char *title = ctab(c)->title ? ctab(c)->title : "";
	const char *mode = ctab(c)->mode == ModeInsert ? "insert" : "normal";

	if (!c->surf_fifo) {
		fprintf(stderr, "spawnuserscript: no fifo\n");
		return;
	}

	if (fork() == 0) {
		setenv("SURF_FIFO", c->surf_fifo, 1);
		setenv("SURF_URL", uri, 1);
		setenv("SURF_TITLE", title, 1);
		setenv("SURF_MODE", mode, 1);

		setenv("QUTE_FIFO", c->surf_fifo, 1);
		setenv("QUTE_URL", uri, 1);
		setenv("QUTE_TITLE", title, 1);
		setenv("QUTE_MODE", mode, 1);

		/* Close all fds >= 3 so parent's sockets/fifo don't leak */
		int _maxfd = (int)sysconf(_SC_OPEN_MAX);
		for (int _fd = 3; _fd < _maxfd; _fd++)
			close(_fd);
		setsid();
		execl("/bin/sh", "sh", "-c", script, NULL);
		fprintf(stderr, "spawnuserscript: exec failed: %s\n", script);
		exit(1);
	}
}

static gchar *
preprocess_userscript(const gchar *script)
{
	/*
	 * Only use main-world injection for scripts that explicitly
	 * need access to the page's JS objects. @grant none just means
	 * "no GM permissions needed" - it does NOT require page world.
	 */
	gboolean needs_main_world = (strstr(script, "unsafeWindow") != NULL &&
								 strstr(script, "@grant unsafeWindow") != NULL);

	if (needs_main_world) {
		GString *out = g_string_new(NULL);

		g_string_append(out,
						"(function(){\n"
						"var unsafeWindow = window;\n"
						"var GM_info = {script:{name:'userscript',version:'1.0'},scriptHandler:'Surf'};\n"
						"function GM_getValue(k,d){try{var v=localStorage.getItem('_gm_'+k);return v!==null?JSON.parse(v):d;}catch(e){return d;}}\n"
						"function GM_setValue(k,v){try{localStorage.setItem('_gm_'+k,JSON.stringify(v));}catch(e){}}\n"
						"function GM_deleteValue(k){try{localStorage.removeItem('_gm_'+k);}catch(e){}}\n"
						"function GM_addStyle(c){var s=document.createElement('style');s.textContent=c;(document.head||document.documentElement).appendChild(s);}\n"
						"function GM_xmlhttpRequest(d){var x=new XMLHttpRequest();x.open(d.method||'GET',d.url,true);x.onload=function(){if(d.onload)d.onload({responseText:x.responseText,status:x.status});};x.send(d.data||null);}\n"
						"var GM={getValue:function(k,d){return Promise.resolve(GM_getValue(k,d));},setValue:function(k,v){GM_setValue(k,v);return Promise.resolve();}};\n");

		g_string_append(out, script);
		g_string_append(out, "\n})();\n");

		return g_string_free(out, FALSE);
	}

	/* All other scripts: wrap in IIFE with GM shims, inject in isolated world */
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
					"\n");

	/* Parse metadata for GM_info */
	const char *meta_start = strstr(script, "// ==UserScript==");
	const char *meta_end = strstr(script, "// ==/UserScript==");
	if (meta_start && meta_end) {
		const char *name_tag = strstr(meta_start, "// @name");
		if (name_tag && name_tag < meta_end) {
			name_tag += 8;
			while (*name_tag == ' ' || *name_tag == '\t')
				name_tag++;
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
			while (*ver_tag == ' ' || *ver_tag == '\t')
				ver_tag++;
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
	const char *runtime_dir;

	if (!c || c->surf_fifo)
		return;

	runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (!runtime_dir || !*runtime_dir)
		runtime_dir = "/tmp";

	c->surf_fifo = g_strdup_printf("%s/surf-fifo-%ld-%s",
								   runtime_dir, (long)getpid(), c->instance_id);

	unlink(c->surf_fifo);
	if (mkfifo(c->surf_fifo, 0600) != 0) {
		fprintf(stderr, "failed to create fifo: %s\n", c->surf_fifo);
		g_free(c->surf_fifo);
		c->surf_fifo = NULL;
		return;
	}

	fd = open(c->surf_fifo, O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		fprintf(stderr, "failed to open fifo: %s\n", c->surf_fifo);
		unlink(c->surf_fifo);
		g_free(c->surf_fifo);
		c->surf_fifo = NULL;
		return;
	}

	c->fifo_chan = g_io_channel_unix_new(fd);
	g_io_channel_set_encoding(c->fifo_chan, NULL, NULL);
	g_io_channel_set_flags(c->fifo_chan,
						   g_io_channel_get_flags(c->fifo_chan) | G_IO_FLAG_NONBLOCK, NULL);
	g_io_channel_set_close_on_unref(c->fifo_chan, TRUE);
	g_io_add_watch(c->fifo_chan, G_IO_IN, fifo_read, c);
}

static void
inject_userscripts_early(WebKitUserContentManager *cm, const char *uri)
{
	char *scriptdir = g_build_filename(g_get_home_dir(), ".surf", "userscripts", NULL);
	GDir *dir = g_dir_open(scriptdir, 0, NULL);
	const char *filename;

	if (!dir) {
		g_free(scriptdir);
		return;
	}

	while ((filename = g_dir_read_name(dir))) {
		if (!g_str_has_suffix(filename, ".user.js"))
			continue;

		char *filepath = g_build_filename(scriptdir, filename, NULL);
		gchar *script = NULL;
		gsize len;

		if (!g_file_get_contents(filepath, &script, &len, NULL) || !len) {
			g_free(filepath);
			continue;
		}

		gboolean is_doc_start = (strstr(script, "@run-at") != NULL &&
								 strstr(script, "document-start") != NULL);

		gboolean needs_page_world = (strstr(script, "unsafeWindow") != NULL &&
									 strstr(script, "@grant unsafeWindow") != NULL);

		GPtrArray *allow = g_ptr_array_new_with_free_func(g_free);
		GPtrArray *exclude = g_ptr_array_new_with_free_func(g_free);
		gboolean has_patterns = FALSE;

		char **lines = g_strsplit(script, "\n", -1);
		for (int i = 0; lines[i]; i++) {
			char *line = g_strstrip(lines[i]);
			if (g_str_has_prefix(line, "// ==/UserScript=="))
				break;
			if (g_str_has_prefix(line, "// @match ")) {
				g_ptr_array_add(allow, g_strstrip(g_strdup(line + 10)));
				has_patterns = TRUE;
			} else if (g_str_has_prefix(line, "// @include ")) {
				g_ptr_array_add(allow, g_strstrip(g_strdup(line + 12)));
				has_patterns = TRUE;
			} else if (g_str_has_prefix(line, "// @exclude ")) {
				g_ptr_array_add(exclude, g_strstrip(g_strdup(line + 12)));
			}
		}
		g_strfreev(lines);

		g_ptr_array_add(allow, NULL);
		g_ptr_array_add(exclude, NULL);

		gchar *processed = preprocess_userscript(script);

		WebKitUserScriptInjectionTime inject_time = is_doc_start ? WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START : WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END;

		const char *const *allow_list = has_patterns ? (const char *const *)allow->pdata : NULL;
		const char *const *exclude_list = has_patterns ? (const char *const *)exclude->pdata : NULL;

		WebKitUserScript *us;

		if (needs_page_world) {
			us = webkit_user_script_new_for_world(
				processed,
				WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
				inject_time,
				"surf-page-world",
				allow_list,
				exclude_list);
		} else {
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

		fprintf(stderr, "userscript loaded (%s%s): %s\n",
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
	struct stat st;
	gchar *contents = NULL;
	gchar **lines;
	int i;

	if (!historyfile)
		return;

	/* Only reload when the file has been modified */
	if (stat(historyfile, &st) == 0) {
		if (history_entries && st.st_mtime == history_mtime)
			return;
		history_mtime = st.st_mtime;
	}

	if (history_entries) {
		for (i = 0; i < (int)history_entries->len; i++) {
			HistoryEntry *e = &g_array_index(history_entries, HistoryEntry, i);
			g_free(e->uri);
			g_free(e->title);
		}
		g_array_free(history_entries, TRUE);
	}

	history_entries = g_array_new(FALSE, TRUE, sizeof(HistoryEntry));

	if (!g_file_get_contents(historyfile, &contents, NULL, NULL) || !contents)
		return;

	lines = g_strsplit(contents, "\n", -1);
	for (i = 0; lines[i]; i++) {
		if (!lines[i][0])
			continue;

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

	gboolean have_contents = g_file_get_contents(historyfile, &contents, NULL, NULL) && contents;

	if (have_contents) {
		gchar **lines = g_strsplit(contents, "\n", -1);
		GString *newcontents = g_string_new(NULL);

		/* Check most recent entry */
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
				if (next_space && *(next_space + 1) && (!title || !*title)) {
					g_free(url_only);
					g_strfreev(lines);
					g_free(contents);
					return;
				}
			}
			g_free(url_only);
			break;
		}

		/* Remove old entries with same URI */
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

	if (title && *title) {
		gchar *safe_title = g_strdup(title);
		/* Strip newlines to avoid corrupting the line-based history format */
		for (gchar *p = safe_title; *p; p++) {
			if (*p == '\n' || *p == '\r')
				*p = ' ';
		}
		fprintf(f, "%ld %s %s\n", (long)time(NULL), uri, safe_title);
		g_free(safe_title);
	} else {
		fprintf(f, "%ld %s\n", (long)time(NULL), uri);
	}
	fclose(f);
}

static void
history_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer data)
{
	Client *c = (Client *)data;
	GtkWidget *label;
	const char *text;

	label = gtk_list_box_row_get_child(row);
	if (!label)
		return;

	text = gtk_widget_get_name(label);
	if (text && *text) {
		Arg a = {.v = text};
		history_hide(c);
		closebar(c);
		loaduri(c, &a);
	}
}

static void
history_hide(Client *c)
{
	if (c->history_scroll) {
		gtk_widget_set_visible(c->history_scroll, FALSE);
	}
	c->history_selected = -1;
}

static void
history_attach(Client *c)
{
	GtkWidget *parent, *before_sb;

	if (!c->history_scroll)
		return;

	parent = gtk_widget_get_parent(c->history_scroll);
	if (parent && parent != c->vbox && GTK_IS_BOX(parent))
		gtk_box_remove(GTK_BOX(parent), c->history_scroll);

	before_sb = gtk_widget_get_first_child(c->vbox);
	while (before_sb && gtk_widget_get_next_sibling(before_sb) != c->statusbar)
		before_sb = gtk_widget_get_next_sibling(before_sb);

	if (parent == c->vbox)
		gtk_box_reorder_child_after(GTK_BOX(c->vbox), c->history_scroll, before_sb);
	else
		gtk_box_insert_child_after(GTK_BOX(c->vbox), c->history_scroll, before_sb);
}

static gboolean
bar_update_filter(gpointer data)
{
	Client *c = (Client *)data;
	const char *text;

	if (ctab(c)->mode != ModeCommand)
		return FALSE;

	text = gtk_editable_get_text(GTK_EDITABLE(c->statentry));
	history_filter(c, text);

	return FALSE;
}

static void
history_filter(Client *c, const char *text)
{
	int count = 0;
	const int max_results = 15;
	GHashTable *seen;
	GtkCssProvider *css;

	if (!history_entries || !text)
		return;

	if (!c->history_scroll) {
		c->history_scroll = gtk_scrolled_window_new();
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(c->history_scroll),
									   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

		c->history_list = gtk_list_box_new();
		gtk_list_box_set_selection_mode(GTK_LIST_BOX(c->history_list),
										GTK_SELECTION_NONE);
		gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(c->history_list), TRUE);
		g_signal_connect(c->history_list, "row-activated",
						 G_CALLBACK(history_row_activated), c);
		gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(c->history_scroll),
									  c->history_list);

		css = gtk_css_provider_new();
		gtk_css_provider_load_from_string(css,
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
										  "  font-size: 13px;"
										  "  padding: 2px 6px;"
										  "  color: #cccccc;"
										  "}");

		gtk_widget_set_name(c->history_scroll, "history-scroll");
		gtk_widget_set_name(c->history_list, "history-list");

		gtk_style_context_add_provider_for_display(
			gdk_display_get_default(),
			GTK_STYLE_PROVIDER(css),
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);

		g_object_set_data_full(G_OBJECT(c->history_list), "css",
							   css, g_object_unref);
	}

	history_attach(c);

	/* Clear existing rows */
	{
		GtkWidget *row;
		while ((row = gtk_widget_get_first_child(c->history_list)))
			gtk_list_box_remove(GTK_LIST_BOX(c->history_list), row);
	}

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

		gtk_list_box_insert(GTK_LIST_BOX(c->history_list), label, -1);

		GtkListBoxRow *row = gtk_list_box_get_row_at_index(
			GTK_LIST_BOX(c->history_list), count);
		(void)row;

		count++;
	}

	g_hash_table_destroy(seen);
	c->history_selected = -1;

	if (count > 0) {
		int row_height = 24;
		int popup_height = count * row_height;
		if (popup_height > 360)
			popup_height = 360;
		gtk_widget_set_size_request(c->history_scroll, -1, popup_height);
		gtk_widget_set_visible(c->history_scroll, TRUE);
	} else if (c->history_scroll) {
		gtk_widget_set_visible(c->history_scroll, FALSE);
	}
}

static void
history_select(Client *c, int direction)
{
	GtkListBoxRow *row;
	int n;

	if (!c->history_list)
		return;

	n = 0;
	while (gtk_list_box_get_row_at_index(GTK_LIST_BOX(c->history_list), n))
		n++;

	if (n == 0)
		return;

	/* Remove old highlight */
	if (c->history_selected >= 0) {
		row = gtk_list_box_get_row_at_index(
			GTK_LIST_BOX(c->history_list), c->history_selected);
		if (row)
			gtk_widget_remove_css_class(GTK_WIDGET(row), "hl");
	}

	if (c->history_selected < 0) {
		if (direction > 0)
			c->history_selected = 0;
		else
			c->history_selected = n - 1;
	} else {
		c->history_selected += direction;
		if (c->history_selected < 0)
			c->history_selected = n - 1;
		if (c->history_selected >= n)
			c->history_selected = 0;
	}

	row = gtk_list_box_get_row_at_index(
		GTK_LIST_BOX(c->history_list), c->history_selected);
	if (row) {
		gtk_widget_add_css_class(GTK_WIDGET(row), "hl");

		GtkWidget *label = gtk_list_box_row_get_child(row);
		if (label) {
			const char *uri = gtk_widget_get_name(label);
			if (uri && *uri) {
				g_signal_handlers_block_by_func(c->statentry,
												bar_on_changed, c);
				gtk_editable_set_text(GTK_EDITABLE(c->statentry), uri);
				gtk_editable_set_position(GTK_EDITABLE(c->statentry), -1);
				g_signal_handlers_unblock_by_func(c->statentry,
												  bar_on_changed, c);
			}
		}
	}
}

static void
tab_pin(Client *c, const Arg *a)
{
	(void)a;
	if (c->tabs_count <= 0)
		return;
	ctab(c)->pinned = !ctab(c)->pinned;
	updatetitle(c);
}

static gboolean
pin_keepalive(gpointer data)
{
	(void)data;
	for (Client *c = clients; c; c = c->next) {
		for (int i = 0; i < c->tabs_count; i++) {
			if (i == c->tabs_active)
				continue;
			if (!c->tabs[i].pinned)
				continue;
			if (!c->tabs[i].view)
				continue;
			if (!webkit_web_view_is_playing_audio(c->tabs[i].view))
				continue;

			webkit_web_view_evaluate_javascript(c->tabs[i].view,
			                                    "void(0);", -1, NULL, NULL, NULL, NULL, NULL);
		}
	}

	return TRUE;
}

static void
updatebar_style(Client *c)
{
	GtkCssProvider *css, *old_css;
	const char *bg, *fg;

	if (curconfig[PrivacyMode].val.i) {
		bg = "#000000";
		fg = "#ffffff";
		goto apply;
	}

	switch (ctab(c)->mode) {
	case ModeInsert:
		bg = "#005f00";
		fg = "#ffffff";
		break;
	case ModeCommand:
		bg = "#1a1a1a";
		fg = "#87afd7";
		break;
	case ModeSearch:
		bg = "#1a1a1a";
		fg = "#d7af5f";
		break;
	case ModeHint:
		bg = "#5f5f00";
		fg = "#ffffff";
		break;
	case ModeSelect:
		bg = "#005f5f";
		fg = "#ffffff";
		break;
	default: /* ModeNormal */
		bg = stat_bg_normal;
		fg = stat_fg_normal;
		break;
	}

apply:
	/* Remove old provider before adding new one to prevent accumulation */
	old_css = g_object_get_data(G_OBJECT(c->statusbar), "bar-css");
	if (old_css) {
		gtk_style_context_remove_provider_for_display(
			gdk_display_get_default(),
			GTK_STYLE_PROVIDER(old_css));
	}

	css = gtk_css_provider_new();
	gchar *cssstr = g_strdup_printf(
		"#surf-statusbar { background-color: %s; }"
		"#surf-barlabel {"
		"  background-color: %s;"
		"  color: %s;"
		"  font: %s;"
		"  padding: 1px 0 1px 6px;"
		"}"
		"#surf-statentry {"
		"  background-color: %s;"
		"  color: %s;"
		"  font: %s;"
		"  border: none; border-radius: 0;"
		"  padding: 1px 6px; min-height: 18px;"
		"}",
		bg, bg, fg, stat_font, bg, fg, stat_font);
	gtk_css_provider_load_from_string(css, cssstr);
	g_free(cssstr);

	gtk_style_context_add_provider_for_display(
		gdk_display_get_default(),
		GTK_STYLE_PROVIDER(css),
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 10);

	/* Transfer ownership to the widget data; g_object_unref called on replace */
	g_object_set_data_full(G_OBJECT(c->statusbar), "bar-css",
						   css, g_object_unref);
}

static void
findcountchanged(WebKitFindController *f, guint count, Client *c)
{
	(void)f;
	ctab(c)->find_match_count = count;
	updatebar(c);
}

static void
foundtext(WebKitFindController *f, guint count, Client *c)
{
	(void)f;
	Tab *t = ctab(c);
	/*
	 * WebKit's found-text signal provides the 1-based index of the
	 * currently highlighted match. This keeps our state in sync with
	 * WebKit's internal selection, eliminating the need for manual
	 * bookkeeping that could desync.
	 */
	t->find_current_match = count;
	updatebar(c);
}

static void
findfailed(WebKitFindController *f, Client *c)
{
	(void)f;
	Tab *t = ctab(c);
	t->find_match_count = 0;
	t->find_current_match = 0;
	updatebar(c);
}

int
main(int argc, char *argv[])
{
	Arg arg;
	Client *c;

	memset(&arg, 0, sizeof(arg));

	ARGBEGIN
	{
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
		/* embedding deprecated */
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
	case 'p':
		defconfig[PrivacyMode].val.i = 0;
		defconfig[PrivacyMode].prio = 2;
		break;
	case 'P':
		defconfig[PrivacyMode].val.i = 1;
		defconfig[PrivacyMode].prio = 2;
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
			die("surf-" VERSION ", see LICENSE for © details\n");
			return 0;
		case 'w':
			showinstanceidflag = 1;
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
	}
	ARGEND;
	if (argc > 0)
		arg.v = argv[0];
	else
		arg.v = "about:blank";

	setup();
	c = newclient(NULL);
	showview(NULL, c);

	loaduri(c, &arg);
	updatetitle(c);

	g_main_loop_run(mainloop);
	cleanup();
	g_main_loop_unref(mainloop);

	return 0;
}
