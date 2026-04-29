# surf-wayland → raw WPE WebKit rewrite plan

Drop GTK4 + WebKitGTK. Embed **WPE WebKit** directly on a self-managed **Wayland** surface. Custom chrome rendered with **Cairo + Pango**. Keep features intact.

This document is the working plan. It is opinionated, exhaustive, and meant to be edited as decisions land.

---

## 1. Goals & non-goals

### Goals
- Remove every `gtk_*` / `gdk_*` / `webkitgtk-6.0` symbol. Final binary links zero GTK.
- Use WPE WebKit (`wpe-webkit-2.0`) via **WPE Platform** (`wpe-platform-1.0`) on a Wayland backend.
- Own the Wayland connection, xdg-shell toplevel, input, clipboard, decoration.
- Keep every user-visible feature listed in `README.md` working.
- Keep `config.h` static-config style (suckless spirit).
- Build remains a single `make`.

### Non-goals (explicit)
- **No X11 fallback.** Wayland-only stays Wayland-only.
- **No accessibility (ATSPI).** Out of scope for v1. Re-evaluate later.
- **No IME on day one.** `text-input-v3` integration is phase 13+.
- **No DRM/headless WPE backends.** Wayland only.
- **No new features.** Parity with current `main` first; features later.

---

## 2. Why "full rewrite" specifically

Phase 1 (tune WebKitGTK) and phase 2 (WPEViewGtk4 embed inside GTK4) were rejected. The motivation is: drop GTK lineage, own the window stack. Trade-offs accepted:

- Multiple weeks of work (estimate: **3–6 weeks** focused, more if hints/userscripts/IME bite).
- Re-implementing dialogs, popups, focus, theming, clipboard, file pickers from scratch.
- Risk of feature regression — current surf has subtle behaviors not obvious from the diff.
- No automatic integration with system theme/dark mode/font config beyond what we wire ourselves.

If at any phase the cost grows beyond appetite, phase 2 (WPE-in-GTK4) remains a viable fallback.

---

## 3. Target architecture

```
┌──────────────────────────────────────────────────────────────┐
│  surf process (UI process)                                   │
│                                                              │
│  ┌────────────────────────────────────────────────────────┐  │
│  │  Wayland connection  (wl_display, registry, seats)     │  │
│  │     ├─ xdg_toplevel "parent" surface                   │  │
│  │     │     ├─ Cairo/Pango chrome (tabbar/status/cmd/dl) │  │
│  │     │     └─ wl_subsurface "page" → WPEView render     │  │
│  │     └─ wl_subsurface(s) for popups (history, hints)    │  │
│  └────────────────────────────────────────────────────────┘  │
│                                                              │
│  ┌────────────────────────────────────────────────────────┐  │
│  │  WPE Platform: WPEDisplayWayland, WPEViewWayland       │  │
│  │  WPE WebKit:   WebKitWebView, WebKitNetworkSession,    │  │
│  │                WebKitWebContext, WebKitSettings,       │  │
│  │                WebKitUserContentManager (kept APIs)    │  │
│  └────────────────────────────────────────────────────────┘  │
│                                                              │
│  ┌────────────────────────────────────────────────────────┐  │
│  │  GLib main loop  (GMainLoop, integrates wl_display fd) │  │
│  └────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────┘
   │ spawns
   ▼
┌──────────────────────────────────────────────────────────────┐
│  WebKit web process(es)  — one per security domain           │
│  Loads webext-surf.so via wpe-web-process-extension-2.0      │
└──────────────────────────────────────────────────────────────┘
```

Key points:
- **WPE owns the Wayland connection.** `WPEDisplayWayland` calls `wl_display_connect()`; we retrieve the fd via `wpe_display_wayland_get_wl_display()`. We do not open a separate `wl_display`.
- **WPEView renders directly onto the xdg_toplevel surface.** `wpe_toplevel_wayland_get_wl_surface()` and `wpe_view_wayland_get_wl_surface()` return the **same** `wl_surface` pointer (verified in Phase 0 spike). There is no page subsurface — WPE fills the toplevel directly.
- **Chrome lives above WPE in subsurfaces.** We create `wl_subsurface` children of WPE's toplevel surface for tabbar, statusbar, command bar, and popups. Each subsurface is placed above the parent (`wl_subsurface_place_above`) so it composites on top of web content. The web page renders underneath chrome areas (slight overdraw; acceptable).
- **`WebKitWebView` constructor.** Use `g_object_new(WEBKIT_TYPE_WEB_VIEW, "display", wpe_display, NULL)`. The `"display"` property (added WPE 2.44) is construct-only. WPE Platform auto-creates `WPEToplevel` + `WPEView` internally.
- Tabs: array of `WebKitWebView`, only one parented into the page subsurface at a time. (Same model as today, no change in concept.)
- Popups (history list, cert window, hint overlay) are extra subsurfaces, **not** xdg popups, to keep them children of our toplevel without compositor pop-up semantics.

---

## 4. Dependency delta

### Removed
| Package | pkg-config name | What it did |
|---|---|---|
| GTK 4 | `gtk4` | Window, widgets, signals, CSS chrome |
| WebKitGTK 6.0 | `webkitgtk-6.0` | Web view, settings, session |
| WebKitGTK web extension | `webkitgtk-web-process-extension-6.0` | Hint detection, JSC bridge |

### Added
| Package | pkg-config name | Purpose |
|---|---|---|
| WPE WebKit 2.0 | `wpe-webkit-2.0` | Web engine (no GTK) |
| WPE Platform | `wpe-platform-2.0` (Wayland: `wpe-platform-wayland-2.0`) | `WPEDisplayWayland` / `WPEViewWayland` |
| WPE Web Extension | `wpe-web-process-extension-2.0` | Replaces WebKitGTK extension lib |
| Wayland client | `wayland-client` | Native Wayland connection |
| Wayland cursor | `wayland-cursor` | Cursor themes |
| Wayland protocols | `wayland-protocols` | XML for xdg-shell, decoration, etc. (build-time) |
| Wayland scanner | `wayland-scanner` | Generates protocol headers (build-time) |
| xkbcommon | `xkbcommon` | Keymap / key event translation |
| Cairo | `cairo` | Chrome drawing |
| Pango / PangoCairo | `pangocairo` | Text layout |
| EGL + wl-egl | `egl wayland-egl` | Likely needed by WPE GPU path |

### Kept
| Package | Why |
|---|---|
| `glib-2.0`, `gobject-2.0`, `gio-2.0` | WPE WebKit transitively requires GLib; we also use `GMainLoop`, `GVariant`, `GHashTable`, `GIOChannel` |

### Wayland protocols to vendor / generate (XML → C from `wayland-protocols`)
- `xdg-shell` — toplevel/popup
- `xdg-decoration-unstable-v1` — server-side decorations when available
- `wp-fractional-scale-v1` — HiDPI fractional scaling
- `wp-viewporter` — used with fractional scale and to size the page subsurface
- `wp-cursor-shape-v1` — server-named cursors (fallback to `wl_cursor` themes)
- `wp-single-pixel-buffer-v1` — cheap solid-color fills (optional)
- `xdg-output-unstable-v1` — monitor logical geometry (also via `wl_output` v4)
- `wl-data-device` (in core wl protocol) — clipboard / primary-selection
- `wp-primary-selection-v1` — primary selection (X11-style middle-click paste)
- `text-input-unstable-v3` — IME (deferred to post-v1)

---

## 5. Build system

`config.mk` becomes:

```make
VERSION = 3.0
PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man
LIBPREFIX = $(PREFIX)/lib
LIBDIR = $(LIBPREFIX)/surf

WLPROTOCOLS_DIR := $(shell pkg-config --variable=pkgdatadir wayland-protocols)
WAYLAND_SCANNER := $(shell pkg-config --variable=wayland_scanner wayland-scanner)

PKG_UI    = wayland-client wayland-cursor xkbcommon cairo pangocairo egl wayland-egl
PKG_WPE   = wpe-webkit-2.0 wpe-platform-wayland-2.0 glib-2.0 gio-2.0
PKG_WEXT  = wpe-web-process-extension-2.0 gio-2.0

UICFLAGS  = `pkg-config --cflags $(PKG_UI) $(PKG_WPE)`
UILIBS    = `pkg-config --libs   $(PKG_UI) $(PKG_WPE)`
WEXTCFLAGS= `pkg-config --cflags $(PKG_WEXT)`
WEXTLIBS  = `pkg-config --libs   $(PKG_WEXT)`

CPPFLAGS += -DVERSION=\"$(VERSION)\" -DLIBPREFIX=\"$(LIBPREFIX)\" \
            -DWEBEXTDIR=\"$(LIBDIR)\" -D_DEFAULT_SOURCE
SURFCFLAGS  = -fPIC $(UICFLAGS)   $(CPPFLAGS) -Wall -Wextra ...
WEBEXTCFLAGS= -fPIC $(WEXTCFLAGS) $(CPPFLAGS) -Wall -Wextra ...
```

`Makefile` adds Wayland protocol code-generation step:

```make
PROTOCOLS = xdg-shell xdg-decoration-unstable-v1 wp-fractional-scale-v1 \
            wp-viewporter wp-cursor-shape-v1 wp-primary-selection-v1
PROTO_SRC = $(PROTOCOLS:%=protocol/%-protocol.c)
PROTO_HDR = $(PROTOCOLS:%=protocol/%-client-protocol.h)

protocol/%-protocol.c: $(WLPROTOCOLS_DIR)/...wayland-protocols.../%.xml
	$(WAYLAND_SCANNER) private-code $< $@
protocol/%-client-protocol.h: ...
	$(WAYLAND_SCANNER) client-header $< $@
```

(Exact paths discovered by `pkg-config --variable=pkgdatadir wayland-protocols` per protocol.)

---

## 6. Module breakdown

The current monolithic `surf.c` (4844 lines) splits into focused units. Header-only types stay in `types.h`.

| File | Responsibility | Approx. lines |
|---|---|---|
| `main.c` | argv, `Client` bringup, GMainLoop | 150 |
| `client.h/c` | `Client` lifecycle, multi-window list, signal-of-death cleanup | 250 |
| `wayland.h/c` | wl_display registry, seat, outputs, fractional scale | 600 |
| `surface.h/c` | xdg_toplevel, subsurfaces, viewport, frame callbacks, repaint scheduling | 350 |
| `input.h/c` | wl_keyboard, wl_pointer, xkbcommon state, key/button event dispatch | 500 |
| `chrome.h/c` | Cairo/Pango painting of tabbar, statusbar, dlbar; layout | 600 |
| `command.h/c` | command-bar editing model (cursor, selection, completion) | 350 |
| `hints.h/c` | hint UI (overlay subsurface, label rendering, key matching) + protocol with web ext | 300 |
| `tabs.h/c` | tab array, switching, pinned tabs, closed-tab stack | 250 |
| `webview.h/c` | WPE `WebKitWebView` wrappers, signal connections, settings application | 600 |
| `download.h/c` | download bar model + progress bookkeeping + GTK-save-dialog replacement | 250 |
| `history.h/c` | `~/.surf/history` parse/serialize, dedup, completion popup | 250 |
| `clipboard.h/c` | wl-data-device, primary selection paste + copy | 250 |
| `userscripts.h/c` | scan `~/.surf/userscripts/`, build GM_* preamble, register with `WebKitUserContentManager` | 250 |
| `tls.h/c` | cert dump window + error page renderer | 150 |
| `screenshot.h/c` | full-page snapshot via WPE → write PNG | 80 |
| `fifo.h/c` | `$SURF_FIFO` channel, command parser | 150 |
| `util.h/c` | small helpers (URL detection, config getters, ipc helpers) | 200 |
| `protocol/*.{c,h}` | generated Wayland protocol bindings | (gen) |
| `webext-surf.c` | port to `wpe-web-process-extension-2.0` | 350 |
| `config.def.h` / `config.h` | keep static-config; rebind GDK_KEY_* → XKB_KEY_* | unchanged shape |

Resulting in ~6k LOC, similar to today, but partitioned.

---

## 7. Phased plan

Each phase ends with a buildable, runnable binary, even if features are stubbed. **Do not start phase N+1 until phase N runs end-to-end.**

### Phase 0 — Spike: prove WPE renders into our Wayland subsurface ✅ DONE

`spike/spike.c` builds and runs. Verified on WPE 2.52.3 / Arch.

Findings:
- `wpe_display_wayland_new()` + `wpe_display_wayland_connect(display, NULL, &err)` connects to `$WAYLAND_DISPLAY`. WPE **owns** the `wl_display`; we retrieve it via `wpe_display_wayland_get_wl_display()`.
- **No** `wpe_view_wayland_new()` — views are created internally when `WebKitWebView` is constructed with `g_object_new(WEBKIT_TYPE_WEB_VIEW, "display", wpe_display, NULL)`.
- `wpe_toplevel_wayland_get_wl_surface()` == `wpe_view_wayland_get_wl_surface()` — same pointer. WPE renders directly onto the xdg_toplevel surface. **Architecture updated accordingly** (see §3).
- `wl_compositor` and `wl_shm` available via `wpe_display_wayland_get_wl_compositor()` / `wpe_display_wayland_get_wl_shm()`. Enough to create our chrome subsurfaces.
- page loads, window renders, GMainLoop integrates cleanly.

Revised approach: chrome = `wl_subsurface` children of WPE's toplevel, placed above via `wl_subsurface_place_above`. We do **not** open a second `wl_display`.

### Phase 1 — Wayland skeleton
- `wayland.c`: registry binding for `wl_compositor`, `wl_subcompositor`, `wl_seat`, `wl_shm`, `xdg_wm_base`, `zxdg_decoration_manager_v1`, `wp_fractional_scale_manager_v1`, `wp_viewporter`, `wp_cursor_shape_manager_v1`, `wl_data_device_manager`, `zwp_primary_selection_device_manager_v1`.
- `surface.c`: open one xdg_toplevel; ack configure; commit blank Cairo paint.
- `input.c`: wl_keyboard with xkbcommon; emit normalized `(xkb_keysym_t, modmask)` events.
- Window opens, blank, takes input, can be closed cleanly.

### Phase 2 — Chrome painting
- Cairo image surface backed by `wl_shm` pool with double buffering.
- Pango layout for status bar text, tab labels.
- Layout pass producing rectangles for: tabbar (top), page (middle), status/dl bar (bottom).
- Repaint scheduled via `wl_surface.frame` callback for vsync; manual damage tracking.
- HiDPI: integer `wl_output.scale` first; fractional scale (`wp_fractional_scale_v1`) wired but optional.
- Replace all `gtk_css_provider_load_from_string()` chunks (`surf.c:2150-2256`, `4358-4401`, `4659-4660`) with concrete Cairo paint calls reading the same color/font config strings.

### Phase 3 — Page surface
- `wl_subsurface` for page region. Position updated on chrome relayout.
- WPEView attached. Frame callbacks bridged.
- Tab switching = swap which `WebKitWebView` is parented; others detached but kept alive (matches today's `gtk_widget_set_visible` toggle, `surf.c:820-895`).

### Phase 4 — Input dispatch
- Two consumers compete for keyboard: chrome (modal logic) and WPEView (when `ModeInsert` and focus is page).
- `input.c` routes based on `c->mode`. In `ModeInsert`/page focus, forward raw events to WPEView via WPE input APIs (`wpe_view_event_dispatch` or equivalent).
- xkbcommon keysym → existing `keys[]` table in `config.h`. Need a keysym remap (`GDK_KEY_*` → `XKB_KEY_*`). Most are 1:1 (`GDK_KEY_a` == `XKB_KEY_a`). Specials: `GDK_KEY_Return` → `XKB_KEY_Return`, etc. Mechanical sed pass.
- Modifier mapping: `GDK_CONTROL_MASK` → our own `MOD_CTRL`; same shape, different defines.

### Phase 5 — Command bar (`o`/`e`/`/`/`Shift+O`)
- Replaces `GtkEntry` (`surf.c:2193`) with a custom edit buffer:
  - text + cursor + selection
  - cursor key handling, kill-line, word movement (mirror what GtkEntry did by default; document any deviations).
  - clipboard paste integration (Phase 11).
- Pango layout for the entry text + caret.
- Reuse the entire callback shape: `baractivate` / `barkeypress` (`surf.c:2197-2199`) become functions called from `input.c` once a complete command is composed.

### Phase 6 — Tabs (visual)
- Replace `GtkBox` tabbar (`surf.c:2135`) with our own clickable rectangles.
- Per-tab state already in `Tab` struct (`common.h:43-78`) — keep unchanged.
- Hover, click, middle-click semantics reproduced (`surf.c:795-797`).

### Phase 7 — WebView wrapper + signals
- Recreate every signal currently on `WebKitWebView` (audit §2 lists 13 signals):
  `notify::estimated-load-progress`, `notify::title`, `close`, `create`, `decide-policy`, `insecure-content-detected`, `load-failed-with-tls-errors`, `load-changed`, `mouse-target-changed`, `permission-request`, `ready-to-show`, `user-message-received`, `web-process-terminated`, `run-file-chooser`. **All exist in WPE WebKit with the same names** — just different header (`<wpe/webkit.h>` instead of `<webkit/webkit.h>`).
- Same applies to `WebKitFindController`, `WebKitWebContext`, `WebKitNetworkSession`, `WebKitCookieManager`, `WebKitUserContentManager`, `WebKitDownload`, all hit-test / policy-decision types — APIs identical between WebKitGTK and WPE WebKit (both produced by the WebKit project).
- Net effect: most of `surf.c:1809-2900` is **header swap + GtkWidget removal**, not logic rewrite.

### Phase 8 — Hints (web extension port)
- `webext-surf.c`: change all
  ```c
  #include <webkit/webkit-web-process-extension.h>
  WebKitWebProcessExtension* ...
  webkit_web_process_extension_initialize(...)
  ```
  to
  ```c
  #include <wpe/webkit-web-process-extension.h>
  /* same type names in WPE WebKit */
  ```
  Type names and JSC API are identical. Confirm at spike.
- `hints.c` (UI side): hint overlay used to live inside the page DOM (rendered by `webext-surf.c:170-222`). **Decision point:** keep DOM-rendered hints (zero UI work, current behavior) **or** render hint labels in our chrome layer (cleaner, no DOM mutation, requires per-frame coordinate sync via `mouse-target-changed`-style messages).
  - Recommend: keep DOM-rendered for v1 (works today), revisit later.
- `webkit_web_view_send_message_to_page()` calls (`surf.c:1050-1165`) and `user-message-received` handling (`surf.c:2486-2515`) work unchanged in WPE.

### Phase 9 — Downloads + file pickers
- `WebKitDownload` API identical. Re-route `download-started` (`surf.c:1888`).
- Replace path-prompt-via-`GtkEntry` (`surf.c:2831-2832`) with our own command bar in "download path" mode. Same UX, our widget.
- Upload picker (`surf.c:2942-3007`): already spawns external `nnn`/`foot`. **Zero change** — it never used GTK file dialogs.

### Phase 10 — Permissions, TLS, error pages
- `permission-request`, `load-failed-with-tls-errors`, `insecure-content-detected` — all WPE-native.
- Cert viewer (`surf.c:3140-3168`): replace `GtkTextView` in `GtkScrolledWindow` with a popup subsurface + Pango text + scroll handling. ~120 LOC.

### Phase 11 — Clipboard
- Replace `gdk_clipboard_*` (`surf.c:1193-3168`) with `wl_data_device` (regular clipboard) and `zwp_primary_selection_device_v1` (X11-style middle-click paste).
- Async read API: keep callback shape (`pasteuri`, `surf.c:3037-3044`), driven by Wayland data offer events.

### Phase 12 — Userscripts, history, FIFO
- `userscripts.c`: zero behavioral change. The 250+ lines that build `GM_*` preamble (`surf.c:3872-3963`) and call `webkit_user_script_new_for_world()` (`surf.c:4072-4080`) all work as-is in WPE.
- History popup (`surf.c:4338-4480`): replace `GtkListBox`/`GtkScrolledWindow` with subsurface listbox (Cairo paint of N rows + selection bg). Reuse existing dedup/scoring logic.
- FIFO (`fifo.c`): unchanged. `GIOChannel` works fine.

### Phase 13 — Screenshots, inspector, polish
- Screenshot (`surf.c:3081-3119`): `webkit_web_view_get_snapshot` on WPE returns a `WebKitImage` / `cairo_surface_t` (verify in spike). If it returns `GdkTexture` like WebKitGTK, port to WPE's image type. PNG write via Cairo.
- Inspector (`surf.c:3273-3281`): WPE WebKit has `WebKitWebInspector` and a remote inspector mode. Easy path: use the remote inspector on `localhost:9222` (`WEBKIT_INSPECTOR_HTTP_SERVER` env), launch user's chosen browser. No embedded inspector window to build. **This is a UX change** — flag it for user.
- Cursor: `wp-cursor-shape-v1` named cursors, fallback to `wl_cursor` theme.
- Fonts: Pango uses fontconfig. The current `stat_font = "monospace 11"` string parses identically with `pango_font_description_from_string`.
- Focus rings, hover states for tabs, command bar.

### Phase 14 — Cleanup
- Delete `display.c` (44 lines, all GTK/GDK).
- Remove `<gtk/gtk.h>` from `common.h:4`.
- Verify zero `gtk_*`/`gdk_*` symbols: `nm surf | grep -E 'gtk_|gdk_'` empty.
- Update `README.md` (Requirements section, Installation).

---

## 8. Concrete API mapping (WebKitGTK → WPE WebKit)

This audit confirms the heavy lifting is **structural** (Wayland, Cairo, Pango), **not WebKit**. WebKit API is largely identical.

| WebKitGTK | WPE WebKit | Notes |
|---|---|---|
| `webkit_web_view_*` | same name | Identical |
| `webkit_settings_*` | same | Identical |
| `webkit_web_context_*` | same | Identical |
| `webkit_network_session_*` | same | Identical |
| `webkit_cookie_manager_*` | same | Identical |
| `webkit_user_content_manager_*` | same | Identical |
| `webkit_download_*`, `webkit_uri_*`, `webkit_*_policy_decision_*`, `webkit_hit_test_result_*` | same | Identical |
| `webkit_web_view_get_snapshot` returns `GdkTexture` | returns `cairo_surface_t` (or skia, version-dep) | **Verify in spike** |
| `webkit_web_view_set_background_color` takes `GdkRGBA` | takes `WebKitColor` | Trivial port |
| `webkit_web_inspector_attach()` (in-window) | Use remote inspector or external window | **UX change** |
| Header: `<webkit/webkit.h>` | `<wpe/webkit.h>` | One-line diff |
| Web ext header: `<webkit/webkit-web-process-extension.h>` | `<wpe/webkit-web-process-extension.h>` | Same |
| `WebKitWebProcessExtension` (lib `webkitgtk-web-process-extension-6.0`) | same name (lib `wpe-web-process-extension-2.0`) | pkg-config swap |

The handful of GDK-typed parameters (`GdkRGBA`, `GdkTexture`) are the only WebKit-side touch-ups.

---

## 9. Config.h migration

`config.h`/`config.def.h` are large but mechanical to port:

- Replace `<gdk/gdk.h>` include implicitly via `common.h` with `<xkbcommon/xkbcommon-keysyms.h>`.
- Mass-replace `GDK_KEY_X` → `XKB_KEY_X`. (Sed-able — names are identical.)
- Replace `GDK_CONTROL_MASK` / `GDK_SHIFT_MASK` / `GDK_MOD1_MASK` with our own `MOD_CTRL` / `MOD_SHIFT` / `MOD_ALT` from xkbcommon mod indices.
- All WebKit `Parameter` entries (`defconfig[ParameterLast]`) untouched.
- `GdkRGBA bgcolor` → keep as our own `RGBA` struct or `cairo_pattern_t *`. Internal-only.

Estimate: ~30 minutes of mechanical edits.

---

## 10. Event loop integration

GLib's `GMainLoop` stays. Add a `GSource` watching `wl_display_get_fd()`:

```c
static gboolean wl_source_check(GSource *s) {
    return wl_display_prepare_read(wl_display) == 0
        && wl_display_dispatch_pending(wl_display) >= 0
        && (poll_revents(...) & G_IO_IN);
}
static gboolean wl_source_dispatch(GSource *s, ...) {
    wl_display_read_events(wl_display);
    wl_display_dispatch_pending(wl_display);
    return G_SOURCE_CONTINUE;
}
```

Standard pattern (used by GTK, Qt-Wayland, Sway tools). One source, attached to the main context, drives the entire app.

WPE Platform's Wayland backend uses *its* own dispatch on the same `wl_display` if we share the connection — this is the open question in Phase 0.

---

## 11. Risks & mitigations

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| WPE Platform won't share wl_display with us | Medium | High | Phase 0 spike. Fallback: run two wl_displays, marshal via cross-process sub-surface or accept a WPE-managed window. |
| `WebKitWebInspector` attached mode unavailable | High | Medium | Use remote inspector (`WEBKIT_INSPECTOR_HTTP_SERVER=9222`); document in README. |
| Snapshot API returns Skia/Cairo type that needs PNG plumbing | Low | Low | Cairo PNG write trivial; verify in Phase 13 spike. |
| IME users blocked | High (for IME users) | High (for them) | Doc as known issue; add `text-input-v3` post-v1. |
| Fractional scaling rounding bugs in chrome | Medium | Medium | Use `wp-viewporter` to map between buffer/logical sizes; test on `1.25x`, `1.5x`, `2x`. |
| HW cursor not available on compositor | Low | Low | `wl_cursor` software fallback. |
| WPE GPU init failure on user's machine (no EGL/llvmpipe issues) | Medium | High | Document Mesa requirement; surface error clearly; offer `LIBGL_ALWAYS_SOFTWARE=1` workaround. |
| Subsurface input routing edge cases (drag from chrome to page) | Medium | Medium | Single `wl_pointer` listener at toplevel level; route based on coords, never rely on subsurface receiving its own pointer events. |
| Drag-and-drop into pages | Low | Medium | Defer; current surf does not support DnD heavily. |
| Tab "create" flow with `WebKitNavigationAction` returns view that needs `ready-to-show` (`surf.c:1925, 1958-1972`) | Medium | Low | Same API in WPE. Keep flow identical. |
| Web extension `.so` ABI mismatch between `wpe-web-process-extension-2.0` and what WebKit's web process expects | Medium | High | Verify the WPE WebKit build on the dev machine ships the matching extension lib version; pin a WPE WebKit version range in README. |

---

## 12. Testing strategy

- **Manual smoke matrix** per phase: open page, type in form (Insert mode), `f` hint, `o` URL, `t` new tab, switch tabs, `/` search, `p` pass, download, screenshot, userscript fires.
- **Reference site list** for parity check: `news.ycombinator.com`, `github.com`, `youtube.com` (HTML5 video), `figma.com` (WebGL), `mail.google.com` (heavy JS), local `file:///` page, `https://self-signed.badssl.com` (TLS error path), `https://expired.badssl.com`.
- **Regression script**: `scripts/smoke.sh` automates URL loads + screenshot comparison (output directory of PNGs to eyeball).
- No unit tests today; not adding for v1. Integration smoke script is enough given suckless-style.

---

## 13. Decisions still needed from user

Before coding starts, decide:

1. **Inspector behavior.** Embedded in-window inspector is non-trivial in WPE. Acceptable to fall back to remote inspector (open in another browser via `WEBKIT_INSPECTOR_HTTP_SERVER=9222`)? **Default proposed: yes.**
2. **EGL hard requirement.** OK to mandate working OpenGL/EGL? (Affects users on bare TTY/llvmpipe-only setups.) **Default proposed: yes, document fallback env var.**
3. **HiDPI fractional scale priority.** Day-1 must-have or Phase 13 polish? **Default proposed: integer scale day 1, fractional in Phase 2.**
4. **DOM-rendered hints vs chrome-rendered hints.** Recommended: keep DOM-rendered (zero work, current behavior).
5. **Acceptable WPE WebKit minimum version.** Suggest pinning to **2.46** or newer (WPE Platform shipped stable; check distro). Need to confirm Arch package version.
6. **Window decorations.** Server-side via `xdg-decoration` when compositor supports (sway/Hyprland do); client-side fallback (which adds ~300 LOC of titlebar drawing). Or no decorations at all (suckless-pure). **Default proposed: SSD when offered, no titlebar otherwise.**
7. **Worktree?** Recommend doing this rewrite on branch `wpe-rewrite`, not `main`. Don't merge until Phase 14 is green on smoke matrix.

---

## 14. Estimated effort

| Phase | Working days |
|---|---|
| 0 spike | 1–2 |
| 1 wayland skeleton | 2 |
| 2 chrome painting | 3 |
| 3 page surface | 1 |
| 4 input dispatch | 2 |
| 5 command bar | 2 |
| 6 tab visuals | 1 |
| 7 webview wrapper + signals | 2 |
| 8 hints (web ext port) | 2 |
| 9 downloads | 1 |
| 10 permissions/TLS | 1 |
| 11 clipboard | 2 |
| 12 userscripts/history/fifo | 1 |
| 13 screenshots/inspector/polish | 2 |
| 14 cleanup | 1 |
| **total** | **~24–28 working days** |

Add 30% buffer for unknowns → **5–6 calendar weeks** at full focus.

---

## 15. Day-zero checklist

Before opening the editor:

- [x] Confirm `wpewebkit` 2.52.3 installed (≥ 2.46). pkg-config name: `wpe-webkit-2.0`.
- [x] Confirm `wpe-platform-2.0` and `wpe-platform-wayland-2.0` exist (not `wpe-platform-1.0`).
- [x] Confirm `wpe-web-process-extension-2.0` exists.
- [x] Created branch `wpe-rewrite`. Tagged `gtk-final` on `main`.
- [x] Built and ran Phase 0 spike (`spike/spike.c`). Loads `https://example.com`. Key findings recorded in §7 Phase 0 and §3.
- [ ] Capture baseline of current surf: open 5 reference sites, save screenshots into `baseline/`. Compare against rewrite output at every phase.
