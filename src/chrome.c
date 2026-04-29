#include "chrome.h"

#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#define _GNU_SOURCE
#include <sys/mman.h>
#include <linux/memfd.h>
#include <fcntl.h>
#include <syscall.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ── Colors (matching existing GTK config) ─────────────────────────────── */
#define COL_TAB_BG       0x1a1a1a
#define COL_TAB_ACTIVE   0x4a4a4a
#define COL_TAB_FG_ON    0xffffff
#define COL_TAB_INACTIVE 0x2a2a2a
#define COL_TAB_FG_OFF   0x888888
#define COL_STAT_BG      0x000000
#define COL_STAT_FG      0xffffff
#define COL_PROG_BAR     0x3a6a9a

#define FONT_CHROME "Terminus (TTF) 10"

static void set_rgb_hex(cairo_t *cr, unsigned int hex)
{
    cairo_set_source_rgb(cr,
        ((hex >> 16) & 0xff) / 255.0,
        ((hex >>  8) & 0xff) / 255.0,
        ( hex        & 0xff) / 255.0);
}

/* ── SHM helpers ────────────────────────────────────────────────────────── */

static int shm_alloc(int size)
{
    int fd = (int)syscall(SYS_memfd_create, "surf-chrome", MFD_CLOEXEC);
    if (fd < 0) {
        perror("memfd_create");
        return -1;
    }
    if (ftruncate(fd, size) < 0) {
        perror("ftruncate");
        close(fd);
        return -1;
    }
    return fd;
}

static void panel_alloc(ChromePanel *p, WaylandState *wl, int w, int h)
{
    int stride = w * 4;
    int size   = stride * h;

    p->fd   = shm_alloc(size);
    p->data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, p->fd, 0);
    p->pool = wl_shm_create_pool(wl->shm, p->fd, size);
    p->buffer = wl_shm_pool_create_buffer(p->pool, 0, w, h, stride,
        WL_SHM_FORMAT_XRGB8888);
    p->csurf  = cairo_image_surface_create_for_data(
        p->data, CAIRO_FORMAT_RGB24, w, h, stride);
    p->width  = w;
    p->height = h;
}

static void panel_free_buffers(ChromePanel *p)
{
    if (p->csurf)  { cairo_surface_destroy(p->csurf);  p->csurf  = NULL; }
    if (p->buffer) { wl_buffer_destroy(p->buffer);     p->buffer = NULL; }
    if (p->pool)   { wl_shm_pool_destroy(p->pool);     p->pool   = NULL; }
    if (p->data && p->width && p->height) {
        munmap(p->data, p->width * p->height * 4);
        p->data = NULL;
    }
    if (p->fd >= 0) { close(p->fd); p->fd = -1; }
}

/* ── Public API ─────────────────────────────────────────────────────────── */

ChromePanel *chrome_panel_create(WaylandState *wl,
    struct wl_surface *parent, int x, int y, int w, int h)
{
    ChromePanel *p = calloc(1, sizeof *p);
    p->fd = -1;

    p->surface    = wl_compositor_create_surface(wl->compositor);
    p->subsurface = wl_subcompositor_get_subsurface(
        wl->subcompositor, p->surface, parent);

    wl_subsurface_set_desync(p->subsurface);
    wl_subsurface_place_above(p->subsurface, parent);
    wl_subsurface_set_position(p->subsurface, x, y);

    panel_alloc(p, wl, w, h);
    return p;
}

void chrome_panel_resize(ChromePanel *p, WaylandState *wl, int w, int h)
{
    if (p->width == w && p->height == h)
        return;
    panel_free_buffers(p);
    panel_alloc(p, wl, w, h);
}

void chrome_panel_set_position(ChromePanel *p, int x, int y)
{
    wl_subsurface_set_position(p->subsurface, x, y);
}

void chrome_panel_destroy(ChromePanel *p)
{
    panel_free_buffers(p);
    if (p->subsurface) wl_subsurface_destroy(p->subsurface);
    if (p->surface)    wl_surface_destroy(p->surface);
    free(p);
}

void chrome_panel_commit(ChromePanel *p)
{
    cairo_surface_flush(p->csurf);
    wl_surface_attach(p->surface, p->buffer, 0, 0);
    wl_surface_damage(p->surface, 0, 0, p->width, p->height);
    wl_surface_commit(p->surface);
}

/* ── Painting ────────────────────────────────────────────────────────────── */

void chrome_paint_tabbar(ChromePanel *p, ChromeTab *tabs, int n)
{
    cairo_t *cr = cairo_create(p->csurf);

    /* background */
    set_rgb_hex(cr, COL_TAB_BG);
    cairo_paint(cr);

    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = pango_font_description_from_string(FONT_CHROME);
    pango_layout_set_font_description(layout, fd);
    pango_font_description_free(fd);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

    int tab_w = n > 0 ? p->width / n : p->width;
    if (tab_w > 200) tab_w = 200;

    for (int i = 0; i < n; i++) {
        int x = i * tab_w;
        int bg = tabs[i].active ? COL_TAB_ACTIVE   : COL_TAB_INACTIVE;
        int fg = tabs[i].active ? COL_TAB_FG_ON    : COL_TAB_FG_OFF;

        /* tab background */
        set_rgb_hex(cr, bg);
        cairo_rectangle(cr, x, 0, tab_w - 1, p->height);
        cairo_fill(cr);

        /* tab label */
        set_rgb_hex(cr, fg);
        pango_layout_set_width(layout, (tab_w - 16) * PANGO_SCALE);
        const char *label = tabs[i].title ? tabs[i].title : "New Tab";
        pango_layout_set_text(layout, label, -1);

        int tw, th;
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_move_to(cr, x + 8, (p->height - th) / 2);
        pango_cairo_show_layout(cr, layout);
    }

    g_object_unref(layout);
    cairo_destroy(cr);
}

void chrome_paint_statusbar(ChromePanel *p, const char *text, int progress,
    int https, int insecure)
{
    cairo_t *cr = cairo_create(p->csurf);

    /* background */
    set_rgb_hex(cr, COL_STAT_BG);
    cairo_paint(cr);

    /* progress bar (thin line at very top of statusbar) */
    if (progress > 0 && progress < 100) {
        int pw = (p->width * progress) / 100;
        set_rgb_hex(cr, COL_PROG_BAR);
        cairo_rectangle(cr, 0, 0, pw, 2);
        cairo_fill(cr);
    }

    /* TLS indicator */
    const char *prefix = "";
    if (https && !insecure) prefix = "🔒 ";
    else if (insecure)      prefix = "⚠ ";

    /* text */
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = pango_font_description_from_string(FONT_CHROME);
    pango_layout_set_font_description(layout, fd);
    pango_font_description_free(fd);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_width(layout, (p->width - 16) * PANGO_SCALE);

    char buf[4096];
    snprintf(buf, sizeof buf, "%s%s", prefix, text ? text : "");
    pango_layout_set_text(layout, buf, -1);

    int tw, th;
    pango_layout_get_pixel_size(layout, &tw, &th);

    set_rgb_hex(cr, COL_STAT_FG);
    cairo_move_to(cr, 8, (p->height - th) / 2);
    pango_cairo_show_layout(cr, layout);

    g_object_unref(layout);
    cairo_destroy(cr);
}
