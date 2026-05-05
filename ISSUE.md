## Current Issue: Web Process Never Renders — Stuck at `progress=0.1`

### What works
- Browser launches, chrome renders correctly
- Custom `SurfDisplay`/`SurfToplevel`/`SurfView` all created, all vfuncs called
- `wpe_display_connect` returns TRUE, EGL initializes on UI side
- DRM device found (`/dev/dri/card0` + `/dev/dri/renderD128`), `WPEDRMDevice` created with valid paths
- `WPEBufferFormats` returned with DMA-BUF formats
- `WPEScreenWayland` created with id=1
- `wpe_view_map`/`wpe_view_set_visible`/`wpe_view_focus_in` all succeed
- `webkit_web_view_load_uri` triggers `is-loading=1` and `progress=0.1`
- Web process spawns, loads libgbm/libdrm/libEGL, sandbox disabled, stays alive
- `web-process-terminated` signal never fires (web process doesn't crash)

### What's broken
- **`render_buffer` vfunc on `SurfView` is NEVER called** — no frames produced
- Page load stalls at `progress=0.1` — never reaches `WEBKIT_LOAD_FINISHED`
- Even `data:text/html,<h1>hi</h1>` stalls — not a network issue
- Even `WEBKIT_SKIA_ENABLE_CPU_RENDERING=1` doesn't help

### Strace evidence from web process (PID 228990)
- Loads `libWPEWebKit`, `libgbm`, `libdrm`, `libEGL` ✅
- Opens `/dev/urandom`, accesses SQLite/HSTS/cookies ✅
- Writes 2 IPC messages to UI process (fd 20) ✅
- Writes 16 eventfd wakeups ✅
- **Never opens `/dev/dri/renderD128`** ❌
- **Never opens ANY `/dev/dri/` device** ❌
- **Never calls `gbm_create_device`** ❌
- No `recvmsg` on IPC socket — never receives data from UI process

### Root cause analysis

The web process loads libgbm/libEGL but **never initializes its renderer**. It stays idle (futex-blocked) without ever opening the DRM device. The expected flow is:

1. UI process calls `drmMainDevice()` → gets DRM paths from our `get_drm_device` → sets `parameters.drmDevice` ✅
2. UI process sends `WebProcessCreationParameters` to web process via IPC (includes `drmDevice`, `rendererBufferTransportMode`, `screenProperties`)
3. Web process receives parameters → `DRMDeviceManager::singleton().initializeMainDevice(parameters.drmDevice)`
4. Web process calls `initializePlatformDisplayIfNeeded()` → tries `mainGBMDevice()` → opens `/dev/dri/renderD128` → `gbm_create_device` → creates `PlatformDisplayGBM`
5. Web process creates `AcceleratedSurface::SwapChain` → renders frames → sends buffers via IPC to UI process

Step 3/4 never happens. The web process never opens the DRM device. Possible reasons:
- `parameters.drmDevice` arrives as null/empty (IPC serialization issue?)
- `DRMDeviceManager::initializeMainDevice` silently fails
- `rendererBufferTransportMode` is empty → web process takes the OLD `libwpe` path instead of `initializePlatformDisplayIfNeeded`
- The `SwapChain` constructor assertion fails silently (previous crash we saw was here)

### Key questions for investigation
1. Is `rendererBufferTransportMode` actually being populated? It requires `usingWPEPlatformAPI=true` + `parameters.drmDevice` non-null. But maybe `usingWPEPlatformAPI` is false?
2. Is `WKWPE::isUsingWPEPlatformAPI()` returning true? It checks if `WPE_TYPE_DISPLAY` class has been initialized — which it should since we create a `SurfDisplay`.
3. Is there a mismatch between what the UI process sends and the web process expects for the `drmDevice` IPC field?
4. Could the web process be taking the `#if USE(WPE_RENDERER)` old path (line 221) with `wpe_loader_init` and failing silently?

### Relevant source files (all under `/home/kek/src/wpewebkit-2.52.3/`)
- `Source/WebKit/UIProcess/glib/DRMMainDevice.cpp` — UI-side DRM device discovery
- `Source/WebKit/UIProcess/glib/WebProcessPoolGLib.cpp:190-220` — transport mode + drmDevice setup
- `Source/WebKit/WebProcess/glib/WebProcessGLib.cpp:209-225` — web process init, GBM init, platform display selection
- `Source/WebKit/WebProcess/WebPage/CoordinatedGraphics/AcceleratedSurface.cpp:654-730` — SwapChain constructor + setupBufferFormat
- `Source/WebKit/Shared/WebProcessCreationParameters.serialization.in:175` — drmDevice IPC serialization (under `#if USE(GBM)`)

### Environment
- WPE WebKit 2.52.3, built from source at `/home/kek/src/wpewebkit-2.52.3/`
- Web process binary: `/usr/local/libexec/wpe-webkit-2.0/WPEWebProcess`
- `libWPEWebKit-2.0.so` at `/usr/local/lib64/`
- All sandbox disabled with `WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1`
- AMD GPU, `/dev/dri/card0` + `/dev/dri/renderD128`, both world-readable
