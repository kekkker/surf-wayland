# surf-wayland — Architecture & Rewrite Plan

Status: **proposal**. Current `wpe-rewrite` branch hit a dead end with chrome rendering. This doc replaces the current chrome architecture with one that doesn't fight WPE.

---

## 1. The Problem We Hit

The browser is a wlroots/dwl Wayland-native fork of `surf` using **WPE WebKit 2.52** (not WebKitGTK + GTK4 as the previous architecture used).

WPE WebKit's `wpe-platform-wayland` plugin owns a Wayland connection of its own:
- Creates its own `wl_display` connection (or shares one)
- Creates its own `wl_compositor`-derived `wl_surface`
- Wraps that surface in an `xdg_surface` + `xdg_toplevel` (`WPEToplevelWayland`)
- Renders web content directly into that toplevel surface (DMA-BUF via `wl_surface.attach`)

We tried to render browser chrome (tabbar, statusbar, dlbar, historybar, command bar, history dropdown) by:

1. Creating a single intermediate `wl_surface` (`chrome_bg`) as a `wl_subsurface` of WPE's toplevel surface
2. Creating chrome panels (each its own `wl_surface`) as subsurfaces of `chrome_bg`
3. Calling `wpe_toplevel_resize()` to shrink WPE's window to make room for chrome above/below
4. Re-raising `chrome_bg` above WPE's toplevel after every layout

**This failed for several reasons that compound:**

### 1.1 dwl is a tiling compositor — `wpe_toplevel_resize` is advisory

dwl/wlroots ignore client-requested sizes for tiled windows. The compositor sends `xdg_toplevel.configure(width, height, states)` dictating the actual size. WPE acks and uses that size. Our `wpe_toplevel_resize(w, h - tab - status)` was a no-op.

Verified via `WAYLAND_DEBUG=client`:
```
xdg_toplevel#28.configure(2558, 1417, array[8])
```
We requested `(1280, 758)`. We got `(2558, 1417)`.

### 1.2 Subsurface-to-buffer clipping

In `wlroots`, child subsurfaces are rendered, but the original 1×1 transparent `chrome_bg` parent buffer caused inconsistent visibility:
- Tabbar at `y=-20` (negative offset) rendered into the gap between dwl tiles — visible
- Statusbar at `y=win_h - status` (inside the toplevel area) didn't render reliably
- Buffer-size dependence is poorly documented; behavior differs across compositors

### 1.3 Coordinate-system gymnastics

`chrome_bg` was positioned at `wl_subsurface.set_position(0, -CHROME_TABBAR_H)` so the tabbar at `chrome_bg(0, 0)` ended up at `toplevel(0, -20)`. This negative-y trick worked for the tabbar but doesn't generalize to the bottom (off-monitor clipping behaves differently).

### 1.4 Z-order with WPE's commit cycle

WPE re-attaches buffers to its toplevel `wl_surface` on every frame. We called `wl_subsurface.place_above(chrome_bg, toplevel)` after WPE commits — but in the trace the toplevel is `wl_surface#26` itself (WPE renders directly into it, not a sub-subsurface). So `place_above` is a no-op for occlusion (subsurfaces are already above their parent in z-order by definition); it only matters for sibling subsurfaces. We were solving a non-problem.

### 1.5 Initial-size race

WPE's `view::resized` signal fires only when the view actually changes size, not at initial map. The first layout call may happen with stale dimensions, leaving the statusbar at the wrong y for the entire session until something else triggers a relayout (like opening the command bar via `o`, which calls `app_relayout_active`).

### 1.6 Conclusion

The current architecture is **fighting WPE WebKit's ownership model.** WPE expects to be the sole owner of an `xdg_toplevel`. We treated its toplevel as a parent we could decorate. Some compositors tolerate this. dwl partially does, partially doesn't. The result is fragile and undebuggable.

**We need to stop being WPE's parasite. We need to own the Wayland window ourselves.**

---

## 2. New Architecture

### 2.1 Inversion of ownership

```
┌─────────────────────────────────────────────────────────────┐
│ OUR xdg_toplevel — wl_surface (root)                        │
│ ┌─────────────────────────────────────────────────────────┐ │
│ │ tabbar (wl_subsurface, anchored top)                    │ │ <- we draw
│ ├─────────────────────────────────────────────────────────┤ │
│ │ dlbar (wl_subsurface, conditional)                      │ │ <- we draw
│ ├─────────────────────────────────────────────────────────┤ │
│ │                                                         │ │
│ │  WPE view (wl_subsurface)                               │ │ <- WPE draws
│ │                                                         │ │   (DMA-BUF /
│ │                                                         │ │    wl_buffer)
│ │                                                         │ │
│ │                                                         │ │
│ ├─────────────────────────────────────────────────────────┤ │
│ │ historybar (wl_subsurface, conditional)                 │ │ <- we draw
│ ├─────────────────────────────────────────────────────────┤ │
│ │ statusbar (wl_subsurface, anchored bottom)              │ │ <- we draw
│ └─────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

**We** own the `xdg_toplevel`. **WPE** renders its web view into a `wl_subsurface` we position. Chrome panels are sibling subsurfaces. No subsurface stacking war, no negative-y tricks, no `place_above` after every WPE commit, no `wpe_toplevel_resize` fights.

When dwl sends `xdg_toplevel.configure(W, H)`:
- We size our toplevel buffer to `W × H` (just transparent background, doesn't really matter — children will cover it)
- We position WPE's view subsurface at `(0, tabbar_h + dlbar_h)` with size `(W, H - tabbar_h - dlbar_h - statusbar_h - historybar_h)`
- We tell WPE that's its size (whatever API allows it — see §3)
- Chrome panels position themselves around it

This is **the only architecture that actually works** with a tiling Wayland compositor whose configure events dictate window size.

### 2.2 Why this beats the alternatives

| Approach | Owns toplevel | WPE fits in | Per-window | Effort |
|----------|---------------|-------------|------------|--------|
| **Current (dead-end)** | WPE | Sibling subsurfaces inside WPE's tree | yes | failed |
| **Layer-shell chrome** | WPE | Independent toplevel | no (per-output only) | low |
| **New (this doc)** | us | Our subsurface | yes | medium |
| **Custom WPE platform plugin** | us, deeply | Our subsurface | yes | high |
| **WPE headless + manual SHM** | us | We pump WPE's buffers to our subsurface | yes | high |

Layer-shell would work for a single-window browser but tying chrome to a screen edge instead of a window is wrong if surf ever supports multiple windows or tiles next to other windows.

---

## 3. The Critical Question: How Does WPE Render Into a Surface We Own?

WPE 2.52 ships three platform plugins: `wpe-platform-wayland`, `wpe-platform-drm`, `wpe-platform-headless`. The Wayland plugin always creates its own xdg_toplevel — there is **no public API to give it a parent `wl_surface` from outside.** Verified via header inspection:

```sh
$ grep wpe_display_wayland_ /usr/local/include/wpe-webkit-2.0/wpe-platform/wpe/wayland/WPEDisplayWayland.h
WPE_API WPEDisplay           *wpe_display_wayland_new              (void);
WPE_API gboolean              wpe_display_wayland_connect          (WPEDisplayWayland *display, const char *socket_name, GError **error);
WPE_API struct wl_display    *wpe_display_wayland_get_wl_display   (WPEDisplayWayland *display);
WPE_API struct wl_compositor *wpe_display_wayland_get_wl_compositor(WPEDisplayWayland *display);
WPE_API struct wl_shm        *wpe_display_wayland_get_wl_shm       (WPEDisplayWayland *display);
```

No `wpe_display_wayland_set_parent_surface` or equivalent. No constructor on `WPEViewWayland` or `WPEToplevelWayland` that takes our surface.

So path "platform-wayland with external parent" is **not available without patching WPE WebKit.** Don't take that path.

### 3.1 Three viable paths, ranked

#### **Path A — Custom WPEPlatform plugin (recommended for new architecture)**

Implement a custom platform by subclassing `WPEDisplay`, `WPEView`, `WPEToplevel`. WPE invokes virtual methods we override to control how its rendered buffers reach the screen.

**Key APIs (WPE 2.52, from `<wpe/wpe-platform.h>`):**
- `WPEDisplay` is a `GObject`-style abstract class. Subclass it, register your own `GType`.
- `WPEView::render_buffer(WPEBuffer*, ...)` is the hot-path callback WPE invokes when it has a new web-content frame. We attach the buffer to our subsurface.
- `WPEBuffer` is the abstract buffer; concrete subclasses are `WPEBufferDMABuf` (with `_get_fd`, `_get_format`, `_get_modifier`) and `WPEBufferSHM` (with `_get_data`, `_get_stride`).
- `wpe_view_buffer_rendered(view, buffer)` and `wpe_view_buffer_released(view, buffer)` — we call these to tell WPE we're done with a buffer.
- `wpe_view_resized(view, width, height)` — we call this when our toplevel configures, telling WPE its drawing area.

**Buffer flow:**

```
WPE renderer ──> wpe_view_render_buffer(WPEBuffer*) ──> our handler
                                                            │
                                                            ▼
                                              build wl_buffer from
                                              WPEBufferDMABuf via
                                              zwp_linux_dmabuf_v1
                                                            │
                                                            ▼
                                       wl_surface_attach(view_subsurface,
                                                         wl_buf, 0, 0)
                                       wl_surface_damage_buffer(...)
                                       wl_surface_commit(view_subsurface)
                                                            │
                                                            ▼
                                       wl_buffer.release event ──>
                                              wpe_view_buffer_released(...)
```

We need:
- `wl_compositor`, `wl_subcompositor` (already have)
- `zwp_linux_dmabuf_v1` (Wayland protocol — `wayland-protocols` package); for DMA-BUF buffer import
- An EGL context if we want to read modifiers (probably not; just import)

**Pros:**
- Cleanest result. Single Wayland connection (ours). WPE has no idea it's not the toplevel.
- We control everything — chrome positions, window decorations, configure handling.
- Works on any wlroots compositor; no compositor-specific quirks.

**Cons:**
- ~600-1000 lines of new code (display/view/toplevel subclasses, dmabuf-import boilerplate).
- Requires understanding WPE's GObject vtable layout. It's not exotic but it's not documented well either.
- DMA-BUF import via `zwp_linux_dmabuf_v1` adds protocol code (~100 lines).

**Reference implementations:**
- WPE's own `wpe-platform-wayland` source: `Source/WebKit/UIProcess/API/wpe/wpe-platform/wayland/` in WPE WebKit. Read this; we're writing our own version of it that uses our existing wl_surface.
- `cog` (https://github.com/Igalia/cog) — official WPE launcher. Has `cog-platform-wl.c` with a similar architecture. Smaller and more readable than WPE's internal one.

#### **Path B — `wpe-platform-headless` + manual blit**

`wpe-platform-headless` renders web content into `WPEBufferSHM` or `WPEBufferDMABuf` without any window-system dependency. We drive the rendering loop and composite the buffers onto our `wl_subsurface` ourselves.

**Pros:**
- No need to subclass anything; use the platform plugin as a black-box renderer.
- Same final result as Path A.

**Cons:**
- Have to manually drive frame timing (no compositor `frame` callback wiring to WPE).
- DMA-BUF flow still required for hardware acceleration.
- Headless platform is meant for testing — performance characteristics not great for a browser. SHM means CPU readback (slow). DMA-BUF works but the headless platform may not negotiate modifiers as smoothly.

Don't pick B unless A turns out to have a deal-breaker.

#### **Path C — wlr-layer-shell for chrome only (fallback)**

Keep WPE owning its `xdg_toplevel`. Move chrome bars (tabbar, statusbar, etc.) into `zwlr_layer_shell_v1` surfaces anchored to the output's top/bottom edges.

**Pros:**
- ~1 day of work. Smallest diff from current code.
- Chrome rendering becomes trivial — layer-shell surfaces have explicit anchor + size + exclusive-zone semantics.
- Solves the "off-screen statusbar" bug today.

**Cons:**
- Per-output, not per-window. If you ever have two surf windows, both share the same chrome.
- Layer-shell anchor reserves output space (exclusive zone) — affects other windows' layouts. Workaround: don't reserve, but then chrome floats over content.
- Window resize/move: chrome stays anchored to output; doesn't follow the window. Wrong feel for a per-window browser chrome.

Pick C only as a stopgap if A is too much work right now.

### 3.2 Recommendation

**Do Path A.** It's medium-effort and produces the right architecture for the long term.

Use Path C if you need something working in 24 hours and intend to revisit later.

Don't do Path B.

---

## 4. Implementation Plan — Path A

### 4.1 New module layout

```
src/
  wayland.{c,h}        — existing; wl_display + globals registry
  wlplatform/          — NEW directory; custom WPE platform
    display.{c,h}      — WPEDisplay subclass; entry point
    view.{c,h}         — WPEView subclass; render_buffer impl
    toplevel.{c,h}     — WPEToplevel subclass; size/state proxy
    dmabuf.{c,h}       — zwp_linux_dmabuf_v1 import helper
  app.h                — AppState; gains our xdg_toplevel + configure handler
  main.c               — bootstrap our toplevel BEFORE creating WPEDisplay
  chrome.c             — unchanged conceptually; subsurfaces now sibling to view
  chrome.h             — unchanged
```

### 4.2 New AppState fields

Add to `src/app.h::AppState`:

```c
/* Our own xdg_toplevel — NOT WPE's */
struct xdg_wm_base       *wm_base;             /* registry global */
struct xdg_surface       *xdg_surface;
struct xdg_toplevel      *xdg_toplevel;
struct wl_surface        *root_surface;        /* root wl_surface, child of xdg_surface */
int                       window_w, window_h;  /* current configure size */
int                       pending_w, pending_h;/* from configure, applied on commit */
gboolean                  configured;          /* first configure received */

/* WPE view subsurface (rendered by our custom platform) */
struct wl_surface        *view_surface;        /* WPE view's surface */
struct wl_subsurface     *view_subsurface;     /* sibling of chrome panels */
int                       view_x, view_y;      /* computed during layout */
int                       view_w, view_h;

/* DMA-BUF import */
struct zwp_linux_dmabuf_v1 *dmabuf;            /* registry global */
```

Remove:
- `chrome_bg`, `chrome_bg_sub`, `chrome_bg_buf`, `chrome_bg_buf_w/h` — no intermediate surface needed
- The old WPE-owned-toplevel refs go away after migration

### 4.3 Bootstrap sequence (replaces current `main.c::main`)

```c
int main(int argc, char **argv) {
    /* 1. Open wl_display, bind globals: wl_compositor, wl_subcompositor,
     *    wl_shm, xdg_wm_base, zwp_linux_dmabuf_v1, wl_seat (for input),
     *    plus existing globals (wl_output etc).                            */
    wayland_connect(&g_app.wl);

    /* 2. Create our xdg_toplevel.                                          */
    g_app.root_surface = wl_compositor_create_surface(g_app.wl.compositor);
    g_app.xdg_surface  = xdg_wm_base_get_xdg_surface(g_app.wl.wm_base,
                                                     g_app.root_surface);
    g_app.xdg_toplevel = xdg_surface_get_toplevel(g_app.xdg_surface);
    xdg_toplevel_set_app_id(g_app.xdg_toplevel, "surf");
    xdg_toplevel_set_title(g_app.xdg_toplevel, "surf");

    /* 3. Wire xdg_surface.configure -> ack + remember size                 */
    xdg_surface_add_listener(g_app.xdg_surface, &xdg_surface_listener, NULL);
    xdg_toplevel_add_listener(g_app.xdg_toplevel, &xdg_toplevel_listener, NULL);

    /* 4. First commit to trigger configure                                 */
    wl_surface_commit(g_app.root_surface);

    /* 5. Roundtrip until configured                                        */
    while (!g_app.configured)
        wl_display_dispatch(g_app.wl.display);

    /* 6. Attach a transparent or solid-color buffer to root_surface.       *
     *    We need *some* buffer here so the surface is mapped. It can be    *
     *    1×1 ARGB transparent — root_surface is fully covered by its       *
     *    children (chrome + view subsurfaces) in normal operation.         *
     *    This buffer must be re-attached on configure if size changes.     */
    attach_root_buffer(g_app.window_w, g_app.window_h);
    wl_surface_commit(g_app.root_surface);

    /* 7. Now create our custom WPE platform.                               */
    g_app.display = WPE_DISPLAY(surf_display_new(&g_app.wl, g_app.root_surface));
    g_app.toplevel = surf_toplevel_new(g_app.display);     /* OUR class */
    /* surf_view_new is invoked indirectly via webkit_web_view_new below.  */

    /* 8. The rest of WPE/WebKit init proceeds normally.                    */
    WebKitNetworkSession *ns = ...;
    Tab *first = tabarray_new(&g_app.tabs, g_app.display, g_app.toplevel, ...);
    /* tabarray_new internally calls webkit_web_view_new_with_session etc; *
     * WebKit calls wpe_view_new, which because we registered our platform *
     * default returns a SurfView (our subclass).                          */

    /* 9. Layout once with the configure size, then enter main loop.        */
    app_layout(g_app.window_w, g_app.window_h);
    g_main_loop_run(g_app.loop);
}
```

### 4.4 Configure handling

```c
static void xdg_toplevel_configure(void *d, struct xdg_toplevel *t,
                                   int32_t w, int32_t h, struct wl_array *st)
{
    g_app.pending_w = w > 0 ? w : winsize[0];
    g_app.pending_h = h > 0 ? h : winsize[1];
    /* states: maximized/fullscreen/etc — track for chrome behavior */
}

static void xdg_surface_configure(void *d, struct xdg_surface *s, uint32_t serial)
{
    xdg_surface_ack_configure(s, serial);
    g_app.configured = true;

    if (g_app.pending_w != g_app.window_w || g_app.pending_h != g_app.window_h) {
        g_app.window_w = g_app.pending_w;
        g_app.window_h = g_app.pending_h;
        attach_root_buffer(g_app.window_w, g_app.window_h);   /* resize bg */
        app_layout(g_app.window_w, g_app.window_h);           /* relayout chrome + view */
    }
}
```

### 4.5 Layout function

```c
void app_layout(int W, int H) {
    /* Tabbar — top */
    chrome_panel_resize_or_create(&g_app.tabbar, g_app.root_surface,
                                  0, 0, W, CHROME_TABBAR_H);

    int top = CHROME_TABBAR_H;

    /* Dlbar — conditional, below tabbar */
    if (g_app.dls.count > 0) {
        int rows = MIN(g_app.dls.count, CHROME_DLBAR_MAX_ROWS);
        int dlh = rows * CHROME_DLROW_H;
        chrome_panel_resize_or_create(&g_app.dlbar, g_app.root_surface,
                                      0, top, W, dlh);
        top += dlh;
    } else if (g_app.dlbar) {
        chrome_panel_destroy(g_app.dlbar);
        g_app.dlbar = NULL;
    }

    /* Statusbar — bottom */
    int sbar_h = cmdbar_panel_height();
    int sbar_y = H - sbar_h;
    chrome_panel_resize_or_create(&g_app.statusbar, g_app.root_surface,
                                  0, sbar_y, W, sbar_h);

    int bottom = sbar_h;

    /* Historybar — conditional, above statusbar */
    if (g_app.cmdbar.mode != CMDBAR_INACTIVE && g_app.history_match_count > 0) {
        int rows = MIN(g_app.history_match_count, sbar_y / CHROME_CMDROW_H);
        int hh = rows * CHROME_CMDROW_H;
        int hy = sbar_y - hh;
        chrome_panel_resize_or_create(&g_app.historybar, g_app.root_surface,
                                      0, hy, W, hh);
        bottom += hh;
    } else if (g_app.historybar) {
        chrome_panel_destroy(g_app.historybar);
        g_app.historybar = NULL;
    }

    /* WPE view — fills the middle */
    int view_x = 0, view_y = top;
    int view_w = W, view_h = H - top - bottom;
    if (g_app.view_subsurface) {
        wl_subsurface_set_position(g_app.view_subsurface, view_x, view_y);
    }
    /* Tell WPE its new drawing area */
    if (g_app.toplevel)
        wpe_toplevel_resized(g_app.toplevel, view_w, view_h);

    g_app.view_x = view_x; g_app.view_y = view_y;
    g_app.view_w = view_w; g_app.view_h = view_h;

    app_repaint_chrome();
    wl_surface_commit(g_app.root_surface);
}
```

### 4.6 Custom WPE platform — minimum viable subclasses

**`SurfDisplay` (subclass of `WPEDisplay`)** — `src/wlplatform/display.{c,h}`

```c
struct _SurfDisplay {
    WPEDisplay parent;
    WaylandState *wl;              /* not owned */
    struct wl_surface *parent_surface;  /* root_surface from AppState */
};

G_DEFINE_TYPE(SurfDisplay, surf_display, WPE_TYPE_DISPLAY)

static gboolean surf_display_connect(WPEDisplay *display, GError **error) {
    /* Already connected via WaylandState; nothing to do. */
    return TRUE;
}

static WPEView *surf_display_create_view(WPEDisplay *display) {
    return WPE_VIEW(surf_view_new(SURF_DISPLAY(display)));
}

static WPEBufferDMABufFormats *surf_display_get_dmabuf_formats(WPEDisplay *d) {
    /* Query zwp_linux_dmabuf_v1 for default-feedback formats. */
    return surf_dmabuf_query_formats(SURF_DISPLAY(d)->wl);
}

static void surf_display_class_init(SurfDisplayClass *klass) {
    WPEDisplayClass *dc = WPE_DISPLAY_CLASS(klass);
    dc->connect = surf_display_connect;
    dc->create_view = surf_display_create_view;
    dc->get_preferred_dma_buf_formats = surf_display_get_dmabuf_formats;
    /* Other vfuncs as needed: get_n_screens, get_screen, ... */
}

SurfDisplay *surf_display_new(WaylandState *wl, struct wl_surface *parent) {
    SurfDisplay *d = g_object_new(SURF_TYPE_DISPLAY, NULL);
    d->wl = wl;
    d->parent_surface = parent;
    return d;
}
```

**`SurfView` (subclass of `WPEView`)** — `src/wlplatform/view.{c,h}`

```c
struct _SurfView {
    WPEView parent;
    struct wl_surface *surface;
    struct wl_subsurface *subsurface;
    /* Map of WPEBuffer* -> wl_buffer* (so we can release back) */
    GHashTable *buf_map;
};

static gboolean surf_view_render_buffer(WPEView *view, WPEBuffer *buffer,
                                        const WPERectangle *damage_rects,
                                        guint n_damage,
                                        GError **error)
{
    SurfView *self = SURF_VIEW(view);
    struct wl_buffer *wl_buf = g_hash_table_lookup(self->buf_map, buffer);

    if (!wl_buf) {
        if (WPE_IS_BUFFER_DMA_BUF(buffer))
            wl_buf = surf_dmabuf_import(g_app.dmabuf, WPE_BUFFER_DMA_BUF(buffer));
        else if (WPE_IS_BUFFER_SHM(buffer))
            wl_buf = surf_shm_import(g_app.wl.shm, WPE_BUFFER_SHM(buffer));
        else { g_set_error(...); return FALSE; }

        g_hash_table_insert(self->buf_map, buffer, wl_buf);

        /* On wl_buffer.release, call wpe_view_buffer_released(view, buffer) */
        static const struct wl_buffer_listener bl = {
            .release = on_wl_buffer_release_static,
        };
        wl_buffer_add_listener(wl_buf, &bl, mk_release_data(view, buffer));
    }

    int w, h;
    wpe_buffer_get_size(buffer, &w, &h);

    wl_surface_attach(self->surface, wl_buf, 0, 0);
    for (guint i = 0; i < n_damage; i++)
        wl_surface_damage_buffer(self->surface,
            damage_rects[i].x, damage_rects[i].y,
            damage_rects[i].width, damage_rects[i].height);
    if (n_damage == 0)
        wl_surface_damage_buffer(self->surface, 0, 0, w, h);

    /* Frame callback — tell WPE when this frame is on screen */
    struct wl_callback *cb = wl_surface_frame(self->surface);
    static const struct wl_callback_listener cl = { .done = on_frame_done };
    wl_callback_add_listener(cb, &cl, mk_frame_data(view, buffer));

    wl_surface_commit(self->surface);
    return TRUE;
}

static void surf_view_class_init(SurfViewClass *klass) {
    WPEViewClass *vc = WPE_VIEW_CLASS(klass);
    vc->render_buffer = surf_view_render_buffer;
    /* Other vfuncs: set_cursor_from_name, lock_pointer, ... */
}
```

**`SurfToplevel` (subclass of `WPEToplevel`)** — `src/wlplatform/toplevel.{c,h}`

Mostly a stub that proxies our window size into WPE's view. WPE's resize/state APIs become our problem to forward back to `xdg_toplevel`:

```c
static gboolean surf_toplevel_resize(WPEToplevel *tl, int w, int h) {
    /* Can't actually resize on tiling compositors. Compositor decides.
     * We could send xdg_toplevel.set_min_size / set_max_size as a hint.
     * For now, return FALSE (resize rejected); WPE handles this. */
    return FALSE;
}

static gboolean surf_toplevel_fullscreen(WPEToplevel *tl) {
    xdg_toplevel_set_fullscreen(g_app.xdg_toplevel, NULL);
    return TRUE;
}

static gboolean surf_toplevel_unfullscreen(WPEToplevel *tl) {
    xdg_toplevel_unset_fullscreen(g_app.xdg_toplevel);
    return TRUE;
}
/* etc — minimize/maximize/title/state */
```

### 4.7 DMA-BUF import — `src/wlplatform/dmabuf.{c,h}`

Use the `zwp_linux_dmabuf_v1` protocol (in `wayland-protocols`). The import helper:

```c
struct wl_buffer *surf_dmabuf_import(struct zwp_linux_dmabuf_v1 *dmabuf,
                                     WPEBufferDMABuf *buffer)
{
    int w, h; wpe_buffer_get_size(WPE_BUFFER(buffer), &w, &h);
    guint32 fmt = wpe_buffer_dma_buf_get_format(buffer);
    guint64 mod = wpe_buffer_dma_buf_get_modifier(buffer);
    guint32 n = wpe_buffer_dma_buf_get_n_planes(buffer);

    struct zwp_linux_buffer_params_v1 *params =
        zwp_linux_dmabuf_v1_create_params(dmabuf);

    for (guint32 i = 0; i < n; i++) {
        zwp_linux_buffer_params_v1_add(params,
            wpe_buffer_dma_buf_get_fd(buffer, i),
            i,
            wpe_buffer_dma_buf_get_offset(buffer, i),
            wpe_buffer_dma_buf_get_stride(buffer, i),
            mod >> 32,
            mod & 0xffffffff);
    }

    struct wl_buffer *wlb = zwp_linux_buffer_params_v1_create_immed(
        params, w, h, fmt, 0);
    zwp_linux_buffer_params_v1_destroy(params);
    return wlb;
}
```

Generate the protocol headers via `wayland-scanner` in the existing build pattern (config.mk already has `WAYLAND_SCANNER`):

```make
src/protocols/linux-dmabuf-v1-client-protocol.h: $(WLPROTOCOLS_DIR)/staging/linux-dmabuf/linux-dmabuf-v1.xml
	$(WAYLAND_SCANNER) client-header $< $@
src/protocols/linux-dmabuf-v1.c: $(WLPROTOCOLS_DIR)/staging/linux-dmabuf/linux-dmabuf-v1.xml
	$(WAYLAND_SCANNER) public-code $< $@
```

### 4.8 What goes away

| Currently in tree | After |
|---|---|
| `chrome_bg` surface + subsurface + buffer | gone |
| `app_raise_chrome` (re-place_above on every resize) | gone |
| `wpe_toplevel_resize` calls in `main`/`actions` | gone (we don't own a WPE toplevel anymore directly; we tell WPE its view size via `wpe_toplevel_resized` from inside our SurfToplevel) |
| `app_get_layout_size` returning view size | replaced by `app_layout` driven by `xdg_surface.configure` |
| `on_view_resized` connected to WPE's `view::resized` | gone — we're the source of truth for size now |

### 4.9 Migration checkpoints

Land in this order, each independently testable:

1. **Add raw xdg_toplevel bootstrap.** Open window, get configure, attach 1×1 buffer, render solid color into root_surface buffer. `surf` shows a colored rectangle at the dwl-assigned size. (~150 lines.)
2. **Migrate chrome panels to be subsurfaces of root_surface.** Tabbar + statusbar render in correct positions. No web content yet. (~50 lines diff.)
3. **Stub WPE custom platform — display + toplevel only.** WebKit initializes; view creation lands but rendering panics or no-ops. Verify GObject registration works. (~200 lines.)
4. **Implement `surf_view_render_buffer` with SHM path only.** Web content visible (CPU-blit). Slow but works. (~150 lines + protocol scaffolding.)
5. **Add DMA-BUF path.** Performance restored. (~150 lines.)
6. **Wire input forwarding** (pointer/keyboard/touch from `wl_seat` → `wpe_view_event(view, ...)`).  (~200 lines, mostly already exists in `input.c`.)
7. **Wire fullscreen / minimize / state changes** through SurfToplevel ↔ xdg_toplevel.

### 4.10 Estimated effort

Path A end-to-end: **3-5 days** of focused work for someone who knows Wayland protocol. Most time goes into (4) + (5): buffer import is finicky.

If unfamiliar with Wayland, double it.

---

## 5. Reference Material

- WPE WebKit source (`Source/WebKit/UIProcess/API/wpe/wpe-platform/`) — `cd /home/kek/src/wpewebkit-2.52.3 && find . -name 'WPE*Wayland*'` for the canonical platform plugin to mimic.
- `cog` — `https://github.com/Igalia/cog`. Minimal WPE launcher; their `cog-platform-wl.c` is the closest existing analog to what we're building.
- `linux-dmabuf-v1.xml` — `pkg-config --variable=pkgdatadir wayland-protocols`/staging/linux-dmabuf/.
- `xdg-shell.xml` — same path, `stable/xdg-shell/`.
- wlroots subsurface/scene-graph docs — for understanding why the current architecture failed: `https://gitlab.freedesktop.org/wlroots/wlroots/-/wikis/`

---

## 6. Open Questions

1. **DMA-BUF feedback v4** — preferred over the older `format` events. Need to handle `default_feedback` from `zwp_linux_dmabuf_v1.get_default_feedback`. Decision: implement v4 from the start; don't bother with the v3 format-event path.
2. **Multi-tab views** — WPE 2.0 supports multiple `WPEView` per `WPEToplevel`. Currently only one is shown at a time (`tabs.active`). With our architecture this means: only the active tab's view subsurface is mapped; others are unmapped (`wl_surface_attach(s, NULL, 0, 0)`). Need to handle map/unmap on tab switch.
3. **Pointer event coordinate translation** — pointer events on root_surface have coordinates relative to root_surface origin; need to subtract `(view_x, view_y)` before forwarding to WPE. Already partially handled in current `input.c::pointer_listener`; will need adjustment.
4. **Server-side decorations** — currently using `zxdg_decoration_manager_v1` to opt out. Continue, since we draw chrome ourselves.
5. **Subpixel/scale handling** — `wl_surface.set_buffer_scale` on root_surface and view_surface. WPE provides `wpe_toplevel_get_scale` which we drive from `wl_output.scale` events.

---

## 7. What NOT to Do

- **Don't try to subclass `WPEDisplayWayland`.** It's `G_DECLARE_FINAL_TYPE`, not subclassable. Subclass `WPEDisplay` directly instead.
- **Don't try to share WPE's wl_display.** Use one `wl_display` connection that we own; pass it into our `SurfDisplay`. Two connections to the same compositor will fight over events.
- **Don't put chrome and the WPE view in different toplevels.** That's the layer-shell anti-architecture. Per-window correctness is required.
- **Don't reintroduce `chrome_bg` as an intermediate parent.** All chrome panels are direct subsurfaces of `root_surface`. The intermediate surface bought us nothing and cost us a buffer-clipping bug.
- **Don't use `wpe_toplevel_resize` to fight the compositor.** Compositor is authoritative. We adapt to its `configure`, not the other way.
