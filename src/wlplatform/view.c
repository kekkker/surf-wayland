#include "view.h"
#include "display.h"
#include "../protocols/linux-dmabuf-v1-client-protocol.h"

#include <wpe/wpe-platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/memfd.h>
#include <fcntl.h>

/* ── Per-buffer bookkeeping ───────────────────────────────────────────── */

typedef struct {
    struct wl_buffer *wl_buffer;
    WPEBuffer        *wpe_buffer;  /* weak ref — may be stale */
    SurfView         *view;        /* owning view */
    /* For DMA-BUF: we create params each time, no need to cache them */
    /* For SHM: pool + mapped data */
    int    shm_fd;
    void  *shm_data;
    int    shm_size;
} BufferEntry;

static void buffer_entry_free(BufferEntry *entry)
{
    if (!entry) return;
    /* Null the back-pointer first so any in-flight wl_buffer.release
     * callback that races with our destruction will bail out safely. */
    entry->view = NULL;
    if (entry->wl_buffer) {
        wl_buffer_destroy(entry->wl_buffer);
        entry->wl_buffer = NULL;
    }
    if (entry->shm_data && entry->shm_size > 0)
        munmap(entry->shm_data, entry->shm_size);
    if (entry->shm_fd >= 0)
        close(entry->shm_fd);
    g_free(entry);
}

/* ── Private data ─────────────────────────────────────────────────────── */

typedef struct _SurfViewPrivate {
    struct wl_surface    *surface;
    struct wl_subsurface *subsurface;
    GHashTable           *buffer_map;   /* WPEBuffer* -> BufferEntry* */
    struct wl_callback   *frame_callback;
} SurfViewPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SurfView, surf_view, WPE_TYPE_VIEW)

/* ── wl_buffer.release callback ───────────────────────────────────────── */

static void on_wl_buffer_release(void *data, struct wl_buffer *wl_buf)
{
    (void)wl_buf;
    BufferEntry *entry = data;

    /* Bail out if the entry is being freed (view nulled by buffer_entry_free)
     * or if the buffer_map was already destroyed (dispose path). */
    if (!entry->view)
        return;

    SurfViewPrivate *priv = surf_view_get_instance_private(entry->view);
    if (!priv->buffer_map)
        return;

    /* If this view has no surface (inactive tab), suppress the release.
     * Otherwise WebKit's AcceleratedBackingStore would tell the web process
     * to reuse the buffer, which triggers another render, which fails
     * (no surface), which completes instantly → infinite render loop. */
    if (!priv->surface)
        return;

    /* Check if the WPEBuffer is still alive and we still track it */
    if (entry->wpe_buffer && WPE_IS_BUFFER(entry->wpe_buffer) &&
        g_hash_table_contains(priv->buffer_map, entry->wpe_buffer)) {
        wpe_view_buffer_released(WPE_VIEW(entry->view), entry->wpe_buffer);
    }
}

static const struct wl_buffer_listener buffer_listener = {
    .release = on_wl_buffer_release,
};

/* ── Frame callback ───────────────────────────────────────────────────── */

static void on_frame_done(void *data, struct wl_callback *cb, uint32_t time)
{
    (void)time;
    SurfView *view = SURF_VIEW(data);
    SurfViewPrivate *priv = surf_view_get_instance_private(view);

    if (priv->frame_callback == cb)
        priv->frame_callback = NULL;
    wl_callback_destroy(cb);

    /* WPE expects buffer_rendered to signal frame completion */
    /* We don't track "current" buffer here; WPE calls render_buffer
     * per frame and we fire buffer_rendered immediately after commit.
     * The actual frame callback is for throttling. */
}

static const struct wl_callback_listener frame_listener = {
    .done = on_frame_done,
};

/* ── DMA-BUF import ───────────────────────────────────────────────────── */

static struct wl_buffer *import_dmabuf(SurfView *view, WPEBufferDMABuf *dma_buf)
{
    SurfViewPrivate *priv = surf_view_get_instance_private(view);
    WPEDisplay *wpe_display = wpe_view_get_display(WPE_VIEW(view));
    SurfDisplay *sdisplay = SURF_DISPLAY(wpe_display);
    struct zwp_linux_dmabuf_v1 *dmabuf_proto = surf_display_get_dmabuf(sdisplay);
    if (!dmabuf_proto) return NULL;

    int w = wpe_buffer_get_width(WPE_BUFFER(dma_buf));
    int h = wpe_buffer_get_height(WPE_BUFFER(dma_buf));
    guint32 fmt = wpe_buffer_dma_buf_get_format(dma_buf);
    guint64 mod = wpe_buffer_dma_buf_get_modifier(dma_buf);
    guint32 n_planes = wpe_buffer_dma_buf_get_n_planes(dma_buf);

    struct zwp_linux_buffer_params_v1 *params =
        zwp_linux_dmabuf_v1_create_params(dmabuf_proto);

    for (guint32 i = 0; i < n_planes; i++) {
        zwp_linux_buffer_params_v1_add(params,
            wpe_buffer_dma_buf_get_fd(dma_buf, i),
            i,
            wpe_buffer_dma_buf_get_offset(dma_buf, i),
            wpe_buffer_dma_buf_get_stride(dma_buf, i),
            (uint32_t)(mod >> 32),
            (uint32_t)(mod & 0xffffffff));
    }

    struct wl_buffer *wl_buf = zwp_linux_buffer_params_v1_create_immed(
        params, w, h, fmt, 0);
    zwp_linux_buffer_params_v1_destroy(params);

    return wl_buf;
}

/* ── SHM import ───────────────────────────────────────────────────────── */

static struct wl_buffer *import_shm(SurfView *view, WPEBufferSHM *shm_buf)
{
    SurfViewPrivate *priv = surf_view_get_instance_private(view);
    WPEDisplay *wpe_display = wpe_view_get_display(WPE_VIEW(view));
    SurfDisplay *sdisplay = SURF_DISPLAY(wpe_display);
    struct wl_shm *wl_shm = surf_display_get_wl_shm(sdisplay);
    if (!wl_shm) return NULL;

    int w = wpe_buffer_get_width(WPE_BUFFER(shm_buf));
    int h = wpe_buffer_get_height(WPE_BUFFER(shm_buf));
    GBytes *bytes = wpe_buffer_shm_get_data(shm_buf);
    guint stride = wpe_buffer_shm_get_stride(shm_buf);

    gsize data_size = g_bytes_get_size(bytes);
    int fd = (int)syscall(SYS_memfd_create, "surf-shm-view", MFD_CLOEXEC);
    if (fd < 0) return NULL;
    if (ftruncate(fd, data_size) < 0) { close(fd); return NULL; }

    void *map = mmap(NULL, data_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) { close(fd); return NULL; }

    /* Copy data into SHM */
    gsize copy_len = 0;
    const void *src = g_bytes_get_data(bytes, &copy_len);
    memcpy(map, src, copy_len);

    struct wl_shm_pool *pool = wl_shm_create_pool(wl_shm, fd, data_size);
    struct wl_buffer *wl_buf = wl_shm_pool_create_buffer(pool, 0,
        w, h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    munmap(map, data_size);
    close(fd);

    return wl_buf;
}

/* ── WPEView::render_buffer vfunc ─────────────────────────────────────── */

static gboolean surf_view_render_buffer(WPEView *view, WPEBuffer *buffer,
    const WPERectangle *damage_rects, guint n_damage, GError **error)
{
    SurfViewPrivate *priv = surf_view_get_instance_private(SURF_VIEW(view));
    if (!priv->surface) {
        /* Expected when this view is inactive (surface handed to another tab).
         * Return FALSE so WebKit backs off gracefully. */
        g_set_error_literal(error, WPE_VIEW_ERROR, WPE_VIEW_ERROR_RENDER_FAILED,
            "No wl_surface attached to view");
        return FALSE;
    }

    /* Look up or create wl_buffer for this WPE buffer */
    BufferEntry *entry = g_hash_table_lookup(priv->buffer_map, buffer);
    if (!entry) {
        struct wl_buffer *wl_buf = NULL;
        if (WPE_IS_BUFFER_DMA_BUF(buffer))
            wl_buf = import_dmabuf(SURF_VIEW(view), WPE_BUFFER_DMA_BUF(buffer));
        else if (WPE_IS_BUFFER_SHM(buffer))
            wl_buf = import_shm(SURF_VIEW(view), WPE_BUFFER_SHM(buffer));
        else {
            g_set_error_literal(error, WPE_VIEW_ERROR, WPE_VIEW_ERROR_RENDER_FAILED,
                "Unknown buffer type");
            return FALSE;
        }
        if (!wl_buf) {
            g_set_error_literal(error, WPE_VIEW_ERROR, WPE_VIEW_ERROR_RENDER_FAILED,
                "Failed to import buffer");
            return FALSE;
        }

        entry = g_new0(BufferEntry, 1);
        entry->wl_buffer = wl_buf;
        entry->shm_fd = -1;
        entry->view = SURF_VIEW(view);
        entry->wpe_buffer = buffer;

        /* Store view pointer in buffer user_data so release callback can find it */
        wpe_buffer_set_user_data(buffer, SURF_VIEW(view), NULL);
        wl_buffer_add_listener(wl_buf, &buffer_listener, entry);

        g_hash_table_insert(priv->buffer_map, buffer, entry);
    }

    int w = wpe_buffer_get_width(buffer);
    int h = wpe_buffer_get_height(buffer);

    /* Attach and damage */
    wl_surface_attach(priv->surface, entry->wl_buffer, 0, 0);
    if (n_damage > 0) {
        for (guint i = 0; i < n_damage; i++)
            wl_surface_damage_buffer(priv->surface,
                damage_rects[i].x, damage_rects[i].y,
                damage_rects[i].width, damage_rects[i].height);
    } else {
        wl_surface_damage_buffer(priv->surface, 0, 0, w, h);
    }

    /* Frame callback for throttling */
    if (!priv->frame_callback) {
        priv->frame_callback = wl_surface_frame(priv->surface);
        wl_callback_add_listener(priv->frame_callback, &frame_listener, SURF_VIEW(view));
    }

    wl_surface_commit(priv->surface);

    /* Tell WPE this buffer is now on screen (simplified; real impl
     * would wait for frame callback, but this works for initial bringup) */
    wpe_view_buffer_rendered(view, buffer);

    return TRUE;
}

/* ── WPEView::set_cursor_from_name vfunc ──────────────────────────────── */

static void surf_view_set_cursor_from_name(WPEView *view, const char *name)
{
    (void)view; (void)name;
    /* Cursor is handled by our own wl_pointer listener in main.c */
}

/* ── WPEView::can_be_mapped vfunc ─────────────────────────────────────── */

static gboolean surf_view_can_be_mapped(WPEView *view)
{
    SurfViewPrivate *priv = surf_view_get_instance_private(SURF_VIEW(view));
    return priv->surface != NULL;
}

/* ── GObject ──────────────────────────────────────────────────────────── */

static void surf_view_dispose(GObject *object)
{
    SurfViewPrivate *priv = surf_view_get_instance_private(SURF_VIEW(object));

    if (priv->frame_callback) {
        wl_callback_destroy(priv->frame_callback);
        priv->frame_callback = NULL;
    }

    /* Clean up all buffer entries. Do NOT destroy the wl_surface /
     * wl_subsurface here — AppState owns those. */
    g_clear_pointer(&priv->buffer_map, g_hash_table_destroy);

    G_OBJECT_CLASS(surf_view_parent_class)->dispose(object);
}

static void surf_view_class_init(SurfViewClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS(klass);
    oclass->dispose = surf_view_dispose;

    WPEViewClass *vclass = WPE_VIEW_CLASS(klass);
    vclass->render_buffer = surf_view_render_buffer;
    vclass->set_cursor_from_name = surf_view_set_cursor_from_name;
    vclass->can_be_mapped = surf_view_can_be_mapped;
}

static void surf_view_init(SurfView *self)
{
    SurfViewPrivate *priv = surf_view_get_instance_private(self);
    priv->buffer_map = g_hash_table_new_full(g_direct_hash, g_direct_equal,
        NULL, (GDestroyNotify)buffer_entry_free);
}

/* ── Public API ───────────────────────────────────────────────────────── */

void surf_view_set_wl_surface(SurfView *view,
                              struct wl_surface *surface,
                              struct wl_subsurface *subsurface)
{
    SurfViewPrivate *priv = surf_view_get_instance_private(view);
    priv->surface = surface;
    priv->subsurface = subsurface;
}

/* Disconnect this view from the shared wl_surface so another tab
 * can take ownership.  Keeps imported wl_buffers alive so the
 * compositor continues showing the old tab's last frame until the
 * new tab overwrites it — avoids background-flash on rapid switches.
 * Buffers are destroyed in surf_view_destroy_buffers() when the
 * tab is actually closed. */
void surf_view_clear_wl_surface(SurfView *view)
{
    SurfViewPrivate *priv = surf_view_get_instance_private(view);

    /* Destroy pending frame callback */
    if (priv->frame_callback) {
        wl_callback_destroy(priv->frame_callback);
        priv->frame_callback = NULL;
    }

    priv->surface = NULL;
    priv->subsurface = NULL;
}

/* Destroy all imported wl_buffers.  Called when a tab is being
 * closed (not on switch) because the WPEBuffers they wrap are
 * about to be freed by WebKit. */
void surf_view_destroy_buffers(SurfView *view)
{
    SurfViewPrivate *priv = surf_view_get_instance_private(view);
    g_hash_table_remove_all(priv->buffer_map);
}

struct wl_surface *surf_view_get_wl_surface(SurfView *view)
{
    SurfViewPrivate *priv = surf_view_get_instance_private(view);
    return priv->surface;
}

struct wl_subsurface *surf_view_get_wl_subsurface(SurfView *view)
{
    SurfViewPrivate *priv = surf_view_get_instance_private(view);
    return priv->subsurface;
}
