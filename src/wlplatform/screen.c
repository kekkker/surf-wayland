#include "screen.h"

#include <wpe/wpe-platform.h>

/* ── SurfScreenSyncObserver ───────────────────────────────────────────────
 *
 * WPEScreenWayland upstream does NOT provide a sync observer, so when
 * WebKit's DisplayRefreshMonitor asks for one it gets NULL and falls back
 * to a 60 Hz software timer — which is why JS rAF/CSS animations cap at
 * ~60 fps regardless of monitor refresh.
 *
 * This subclass drives sync events from a GLib main-loop timer set at the
 * screen's reported refresh rate. It's not vsync-locked, but it gives
 * WebKit a non-60 Hz cadence to drive rAF off of. */

#define SURF_TYPE_SCREEN_SYNC_OBSERVER (surf_screen_sync_observer_get_type())
G_DECLARE_FINAL_TYPE(SurfScreenSyncObserver, surf_screen_sync_observer,
    SURF, SCREEN_SYNC_OBSERVER, WPEScreenSyncObserver)

struct _SurfScreenSyncObserver {
    WPEScreenSyncObserver parent;
    int    interval_ms;   /* derived from refresh rate */
    guint  timer_id;
};

G_DEFINE_TYPE(SurfScreenSyncObserver, surf_screen_sync_observer,
    WPE_TYPE_SCREEN_SYNC_OBSERVER)

static gboolean sync_tick(gpointer data)
{
    WPEScreenSyncObserver *observer = WPE_SCREEN_SYNC_OBSERVER(data);
    /* Chain up to the abstract base's sync() which iterates registered
     * callbacks and invokes them. */
    WPEScreenSyncObserverClass *base = WPE_SCREEN_SYNC_OBSERVER_CLASS(
        surf_screen_sync_observer_parent_class);
    if (base->sync)
        base->sync(observer);
    return G_SOURCE_CONTINUE;
}

static void surf_screen_sync_observer_start(WPEScreenSyncObserver *observer)
{
    SurfScreenSyncObserver *self = (SurfScreenSyncObserver *)observer;
    if (self->timer_id || self->interval_ms <= 0)
        return;
    self->timer_id = g_timeout_add(self->interval_ms, sync_tick, observer);
}

static void surf_screen_sync_observer_stop(WPEScreenSyncObserver *observer)
{
    SurfScreenSyncObserver *self = (SurfScreenSyncObserver *)observer;
    if (self->timer_id) {
        g_source_remove(self->timer_id);
        self->timer_id = 0;
    }
}

static void surf_screen_sync_observer_dispose(GObject *object)
{
    SurfScreenSyncObserver *self = (SurfScreenSyncObserver *)object;
    if (self->timer_id) {
        g_source_remove(self->timer_id);
        self->timer_id = 0;
    }
    G_OBJECT_CLASS(surf_screen_sync_observer_parent_class)->dispose(object);
}

static void surf_screen_sync_observer_class_init(
    SurfScreenSyncObserverClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS(klass);
    oclass->dispose = surf_screen_sync_observer_dispose;

    WPEScreenSyncObserverClass *sclass = WPE_SCREEN_SYNC_OBSERVER_CLASS(klass);
    sclass->start = surf_screen_sync_observer_start;
    sclass->stop  = surf_screen_sync_observer_stop;
    /* sync vfunc inherited from base; chains through callbacks. */
}

static void surf_screen_sync_observer_init(SurfScreenSyncObserver *self)
{
    self->interval_ms = 0;
    self->timer_id = 0;
}

static SurfScreenSyncObserver *surf_screen_sync_observer_new(int refresh_mhz)
{
    SurfScreenSyncObserver *o = g_object_new(SURF_TYPE_SCREEN_SYNC_OBSERVER, NULL);
    int hz = refresh_mhz / 1000;
    if (hz < 24) hz = 60;
    if (hz > 1000) hz = 1000;
    int ms = 1000 / hz;
    if (ms < 1) ms = 1;
    o->interval_ms = ms;
    return o;
}

/* ── SurfScreen ───────────────────────────────────────────────────────── */

struct _SurfScreen {
    WPEScreen parent;
    SurfScreenSyncObserver *observer;
};

G_DEFINE_TYPE(SurfScreen, surf_screen, WPE_TYPE_SCREEN)

static WPEScreenSyncObserver *surf_screen_get_sync_observer(WPEScreen *screen)
{
    SurfScreen *self = (SurfScreen *)screen;
    if (!self->observer) {
        int refresh = wpe_screen_get_refresh_rate(screen);
        self->observer = surf_screen_sync_observer_new(refresh);
    }
    return WPE_SCREEN_SYNC_OBSERVER(self->observer);
}

static void surf_screen_invalidate(WPEScreen *screen)
{
    SurfScreen *self = (SurfScreen *)screen;
    g_clear_object(&self->observer);
    /* Chain up if base has an invalidate implementation. */
    WPEScreenClass *base = WPE_SCREEN_CLASS(surf_screen_parent_class);
    if (base->invalidate)
        base->invalidate(screen);
}

static void surf_screen_dispose(GObject *object)
{
    SurfScreen *self = (SurfScreen *)object;
    g_clear_object(&self->observer);
    G_OBJECT_CLASS(surf_screen_parent_class)->dispose(object);
}

static void surf_screen_class_init(SurfScreenClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS(klass);
    oclass->dispose = surf_screen_dispose;

    WPEScreenClass *sclass = WPE_SCREEN_CLASS(klass);
    sclass->get_sync_observer = surf_screen_get_sync_observer;
    sclass->invalidate = surf_screen_invalidate;
}

static void surf_screen_init(SurfScreen *self)
{
    self->observer = NULL;
}

WPEScreen *surf_screen_new(guint id)
{
    return WPE_SCREEN(g_object_new(SURF_TYPE_SCREEN, "id", id, NULL));
}
