#include "display.h"
#include "../protocols/xdg-shell-client-protocol.h"
#include "../protocols/linux-dmabuf-v1-client-protocol.h"

#include <wpe/wpe-platform.h>
#include <wpe/wayland/wpe-wayland.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <xf86drm.h>
#include <drm/drm.h>
#include <libdrm/drm_fourcc.h>

/* ── Forward declarations for our subclasses ──────────────────────────── */
#include "view.h"
#include "toplevel.h"

/* ── Private instance data ────────────────────────────────────────────── */

typedef struct _SurfDisplayPrivate {
    struct wl_display        *wl_display;
    struct wl_compositor     *compositor;
    struct wl_subcompositor  *subcompositor;
    struct wl_shm            *shm;
    struct xdg_wm_base       *wm_base;
    struct zwp_linux_dmabuf_v1 *dmabuf;

    /* Collected DMA-BUF format/modifier pairs from compositor */
    GArray   *dmabuf_formats;       /* of struct { uint32_t format; uint64_t modifier; } */
    gboolean dmabuf_formats_done;

    /* DRM device for buffer allocation */
    WPEDRMDevice *drm_device;

    /* Screen */
    WPEScreen *screen;

    /* GLib main loop integration */
    GSource  *event_source;
} SurfDisplayPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SurfDisplay, surf_display, WPE_TYPE_DISPLAY)

/* ── DMA-BUF format tracking ──────────────────────────────────────────── */

typedef struct {
    uint32_t format;
    uint64_t modifier;
} DmaBufFormatMod;

static void dmabuf_modifier(void *data, struct zwp_linux_dmabuf_v1 *dmabuf,
    uint32_t format, uint32_t modifier_hi, uint32_t modifier_lo)
{
    SurfDisplayPrivate *priv = data;
    DmaBufFormatMod fm = {
        .format = format,
        .modifier = ((uint64_t)modifier_hi << 32) | modifier_lo
    };
    g_array_append_val(priv->dmabuf_formats, fm);
}

static void dmabuf_format(void *data, struct zwp_linux_dmabuf_v1 *dmabuf,
    uint32_t format)
{
    /* Legacy v3 format event — add with DRM_FORMAT_MOD_INVALID */
    SurfDisplayPrivate *priv = data;
    DmaBufFormatMod fm = { .format = format, .modifier = 0ULL }; /* LINEAR */
    g_array_append_val(priv->dmabuf_formats, fm);
}

static const struct zwp_linux_dmabuf_v1_listener dmabuf_listener = {
    .format   = dmabuf_format,
    .modifier = dmabuf_modifier,
};

/* ── Wayland event source (GLib main loop integration) ─────────────────── */

typedef struct {
    GSource  source;
    GPollFD  pfd;
    SurfDisplay *display;
    gboolean reading;
} SurfEventSource;

static gboolean event_source_prepare(GSource *base, gint *timeout)
{
    SurfEventSource *src = (SurfEventSource *)base;
    SurfDisplayPrivate *priv = surf_display_get_instance_private(src->display);

    *timeout = -1;

    if (src->reading)
        return FALSE;

    while (wl_display_prepare_read(priv->wl_display)) {
        if (wl_display_dispatch_pending(priv->wl_display) < 0)
            return FALSE;
    }
    wl_display_flush(priv->wl_display);
    src->reading = TRUE;
    return FALSE;
}

static gboolean event_source_check(GSource *base)
{
    SurfEventSource *src = (SurfEventSource *)base;
    if (src->pfd.revents & (G_IO_ERR | G_IO_HUP))
        return FALSE;
    if (src->reading && !(src->pfd.revents & G_IO_IN)) {
        SurfDisplayPrivate *priv = surf_display_get_instance_private(src->display);
        wl_display_cancel_read(priv->wl_display);
        src->reading = FALSE;
    }
    return !!(src->pfd.revents & G_IO_IN);
}

static gboolean event_source_dispatch(GSource *base, GSourceFunc cb, gpointer data)
{
    SurfEventSource *src = (SurfEventSource *)base;
    SurfDisplayPrivate *priv = surf_display_get_instance_private(src->display);

    if (src->pfd.revents & (G_IO_ERR | G_IO_HUP)) {
        GError *err = g_error_new_literal(WPE_DISPLAY_ERROR,
            WPE_DISPLAY_ERROR_CONNECTION_LOST,
            "Wayland connection lost");
        wpe_display_disconnected(WPE_DISPLAY(src->display), err);
        return FALSE;
    }

    if (src->reading && (src->pfd.revents & G_IO_IN)) {
        if (wl_display_read_events(priv->wl_display) < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                GError *err = g_error_new(WPE_DISPLAY_ERROR,
                    WPE_DISPLAY_ERROR_CONNECTION_LOST,
                    "Wayland read error: %s", g_strerror(errno));
                wpe_display_disconnected(WPE_DISPLAY(src->display), err);
                return FALSE;
            }
        }
        src->reading = FALSE;
    } else if (src->reading) {
        wl_display_cancel_read(priv->wl_display);
        src->reading = FALSE;
    }

    if (wl_display_dispatch_pending(priv->wl_display) < 0)
        return FALSE;

    src->pfd.revents = 0;
    return TRUE;
}

static void event_source_finalize(GSource *base)
{
    SurfEventSource *src = (SurfEventSource *)base;

    if (src->reading) {
        SurfDisplayPrivate *priv = surf_display_get_instance_private(src->display);
        wl_display_cancel_read(priv->wl_display);
        src->reading = FALSE;
    }
}

static GSourceFuncs event_source_funcs = {
    .prepare  = event_source_prepare,
    .check    = event_source_check,
    .dispatch = event_source_dispatch,
    .finalize = event_source_finalize,
    .closure_callback = NULL,
    .closure_marshal = NULL,
};

static GSource *create_event_source(SurfDisplay *display)
{
    SurfDisplayPrivate *priv = surf_display_get_instance_private(display);
    GSource *base = g_source_new(&event_source_funcs, sizeof(SurfEventSource));
    SurfEventSource *src = (SurfEventSource *)base;
    src->display = display;
    src->pfd.fd = wl_display_get_fd(priv->wl_display);
    src->pfd.events = G_IO_IN | G_IO_ERR | G_IO_HUP;
    src->pfd.revents = 0;
    src->reading = FALSE;
    g_source_add_poll(base, &src->pfd);
    g_source_set_priority(base, G_PRIORITY_DEFAULT);
    g_source_set_can_recurse(base, TRUE);
    g_source_attach(base, g_main_context_get_thread_default());
    return base;
}

/* ── WPEDisplay vfuncs ────────────────────────────────────────────────── */

static gboolean surf_display_connect(WPEDisplay *display, GError **error)
{
    /* Already connected — wayland_init did it before we were created */
    SurfDisplayPrivate *priv = surf_display_get_instance_private(SURF_DISPLAY(display));

    /* Collect DMA-BUF formats if available */
    if (priv->dmabuf) {
        zwp_linux_dmabuf_v1_add_listener(priv->dmabuf, &dmabuf_listener, priv);
        wl_display_roundtrip(priv->wl_display);
    }

    priv->event_source = create_event_source(SURF_DISPLAY(display));

    /* Create a default screen (use WPEScreenWayland — base WPEScreen is abstract) */
    priv->screen = WPE_SCREEN(g_object_new(WPE_TYPE_SCREEN_WAYLAND, "id", 1, NULL));
    wpe_screen_set_size(priv->screen, 1920, 1080);
    wpe_screen_set_physical_size(priv->screen, 508, 285); /* ~96 DPI */
    wpe_screen_set_scale(priv->screen, 1.0);
    wpe_screen_set_refresh_rate(priv->screen, 60000); /* 60 Hz in mHz */
    wpe_display_screen_added(display, priv->screen);

    return TRUE;
}

static gpointer surf_display_get_egl_display(WPEDisplay *display, GError **error)
{
    SurfDisplayPrivate *priv = surf_display_get_instance_private(SURF_DISPLAY(display));
    static EGLDisplay egl_display = NULL;

    if (egl_display)
        return egl_display;

    egl_display = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR,
        priv->wl_display, NULL);
    if (egl_display == EGL_NO_DISPLAY) {
        g_set_error_literal(error, WPE_DISPLAY_ERROR,
            WPE_DISPLAY_ERROR_CONNECTION_LOST,
            "Failed to get EGL display for Wayland");
        return NULL;
    }

    EGLint major, minor;
    if (!eglInitialize(egl_display, &major, &minor)) {
        g_set_error_literal(error, WPE_DISPLAY_ERROR,
            WPE_DISPLAY_ERROR_CONNECTION_LOST,
            "Failed to initialize EGL display");
        egl_display = NULL;
        return NULL;
    }

    return egl_display;
}

static WPEView *surf_display_create_view(WPEDisplay *display)
{
    return WPE_VIEW(g_object_new(SURF_TYPE_VIEW, "display", display, NULL));
}

static WPEToplevel *surf_display_create_toplevel(WPEDisplay *display, guint max_views)
{
    return WPE_TOPLEVEL(g_object_new(SURF_TYPE_TOPLEVEL,
        "display", display, "max-views", max_views, NULL));
}

static WPEDRMDevice *surf_display_get_drm_device(WPEDisplay *display)
{
    SurfDisplayPrivate *priv = surf_display_get_instance_private(SURF_DISPLAY(display));
    if (priv->drm_device)
        return priv->drm_device;

    /* Try to find a render node */
    int fd = -1;
    const char *render_nodes[] = {
        "/dev/dri/renderD128",
        "/dev/dri/renderD129",
        NULL
    };
    for (int i = 0; render_nodes[i]; i++) {
        fd = open(render_nodes[i], O_RDWR | O_CLOEXEC);
        if (fd >= 0) {
            drmDevicePtr device = NULL;
            if (drmGetDevice(fd, &device) == 0 && device) {
                char *primary = NULL;
                char *render = NULL;
                if (device->available_nodes & (1 << DRM_NODE_PRIMARY))
                    primary = g_strdup(device->nodes[DRM_NODE_PRIMARY]);
                if (device->available_nodes & (1 << DRM_NODE_RENDER))
                    render = g_strdup(device->nodes[DRM_NODE_RENDER]);
                priv->drm_device = wpe_drm_device_new(primary, render);
                drmFreeDevice(&device);
                close(fd);
                g_free(primary);
                g_free(render);
                return priv->drm_device;
            }
            close(fd);
        }
    }

    g_warning("surf: no DRM device found");
    return NULL;
}

static WPEBufferFormats *surf_display_get_preferred_buffer_formats(WPEDisplay *display)
{
    SurfDisplayPrivate *priv = surf_display_get_instance_private(SURF_DISPLAY(display));

    WPEDRMDevice *device = surf_display_get_drm_device(display);

    if (!priv->dmabuf || priv->dmabuf_formats->len == 0) {
        /* Fallback: advertise basic formats with INVALID modifier */
        WPEBufferFormatsBuilder *builder = wpe_buffer_formats_builder_new(device);
        wpe_buffer_formats_builder_append_group(builder, device, WPE_BUFFER_FORMAT_USAGE_RENDERING);
        /* AR24 = DRM_FORMAT_ARGB8888, XR24 = DRM_FORMAT_XRGB8888 */
        wpe_buffer_formats_builder_append_format(builder, 0x34325241, DRM_FORMAT_MOD_INVALID);
        wpe_buffer_formats_builder_append_format(builder, 0x34325258, DRM_FORMAT_MOD_INVALID);
        return wpe_buffer_formats_builder_end(builder);
    }

    WPEBufferFormatsBuilder *builder = wpe_buffer_formats_builder_new(device);
    wpe_buffer_formats_builder_append_group(builder, device, WPE_BUFFER_FORMAT_USAGE_RENDERING);
    for (guint i = 0; i < priv->dmabuf_formats->len; i++) {
        DmaBufFormatMod *fm = &g_array_index(priv->dmabuf_formats, DmaBufFormatMod, i);
        wpe_buffer_formats_builder_append_format(builder, fm->format, fm->modifier);
    }
    return wpe_buffer_formats_builder_end(builder);
}

static guint surf_display_get_n_screens(WPEDisplay *display)
{
    (void)display;
    return 1;  /* TODO: track wl_output globals */
}

static WPEScreen *surf_display_get_screen(WPEDisplay *display, guint index)
{
    SurfDisplayPrivate *priv = surf_display_get_instance_private(SURF_DISPLAY(display));
    if (index == 0 && priv->screen)
        return priv->screen;
    return NULL;  /* TODO: track wl_output globals */
}

static void surf_display_dispose(GObject *object)
{
    SurfDisplayPrivate *priv = surf_display_get_instance_private(SURF_DISPLAY(object));

    if (priv->event_source) {
        g_source_destroy(priv->event_source);
        g_clear_pointer(&priv->event_source, g_source_unref);
    }
    if (priv->dmabuf_formats)
        g_clear_pointer(&priv->dmabuf_formats, g_array_unref);

    g_clear_pointer(&priv->drm_device, wpe_drm_device_unref);

    g_clear_object(&priv->screen);

    /* Do NOT destroy wl_display, compositor, etc — WaylandState owns them */

    G_OBJECT_CLASS(surf_display_parent_class)->dispose(object);
}

static void surf_display_class_init(SurfDisplayClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS(klass);
    oclass->dispose = surf_display_dispose;

    WPEDisplayClass *dclass = WPE_DISPLAY_CLASS(klass);
    dclass->connect = surf_display_connect;
    dclass->create_view = surf_display_create_view;
    dclass->create_toplevel = surf_display_create_toplevel;
    dclass->get_preferred_buffer_formats = surf_display_get_preferred_buffer_formats;
    dclass->get_egl_display = surf_display_get_egl_display;
    dclass->get_n_screens = surf_display_get_n_screens;
    dclass->get_screen = surf_display_get_screen;
    dclass->get_drm_device = surf_display_get_drm_device;
}

static void surf_display_init(SurfDisplay *self)
{
    SurfDisplayPrivate *priv = surf_display_get_instance_private(self);
    priv->dmabuf_formats = g_array_new(FALSE, FALSE, sizeof(DmaBufFormatMod));
}

/* ── Public API ───────────────────────────────────────────────────────── */

SurfDisplay *surf_display_new(struct wl_display *wl_display,
                              struct wl_compositor *compositor,
                              struct wl_subcompositor *subcompositor,
                              struct wl_shm *shm,
                              struct xdg_wm_base *wm_base,
                              struct zwp_linux_dmabuf_v1 *dmabuf)
{
    SurfDisplay *d = SURF_DISPLAY(g_object_new(SURF_TYPE_DISPLAY, NULL));
    SurfDisplayPrivate *priv = surf_display_get_instance_private(d);
    priv->wl_display   = wl_display;
    priv->compositor   = compositor;
    priv->subcompositor = subcompositor;
    priv->shm          = shm;
    priv->wm_base      = wm_base;
    priv->dmabuf       = dmabuf;
    return d;
}

struct wl_display *surf_display_get_wl_display(SurfDisplay *d)
{
    SurfDisplayPrivate *priv = surf_display_get_instance_private(d);
    return priv->wl_display;
}

struct wl_compositor *surf_display_get_wl_compositor(SurfDisplay *d)
{
    SurfDisplayPrivate *priv = surf_display_get_instance_private(d);
    return priv->compositor;
}

struct wl_subcompositor *surf_display_get_wl_subcompositor(SurfDisplay *d)
{
    SurfDisplayPrivate *priv = surf_display_get_instance_private(d);
    return priv->subcompositor;
}

struct wl_shm *surf_display_get_wl_shm(SurfDisplay *d)
{
    SurfDisplayPrivate *priv = surf_display_get_instance_private(d);
    return priv->shm;
}

struct xdg_wm_base *surf_display_get_xdg_wm_base(SurfDisplay *d)
{
    SurfDisplayPrivate *priv = surf_display_get_instance_private(d);
    return priv->wm_base;
}

struct zwp_linux_dmabuf_v1 *surf_display_get_dmabuf(SurfDisplay *d)
{
    SurfDisplayPrivate *priv = surf_display_get_instance_private(d);
    return priv->dmabuf;
}

void surf_display_update_screen_size(SurfDisplay *d, int width, int height)
{
    SurfDisplayPrivate *priv = surf_display_get_instance_private(d);
    if (!priv->screen)
        return;
    wpe_screen_set_size(priv->screen, width, height);
    /* Assume 96 DPI for physical size */
    wpe_screen_set_physical_size(priv->screen, width * 254 / 960, height * 254 / 960);
    wpe_screen_invalidate(priv->screen);
}
