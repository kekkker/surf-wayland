#include "display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gdk/gdk.h>

#ifdef WAYLAND_SUPPORT
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <gdk/gdkwayland.h>
#endif

#ifdef X11_SUPPORT
#include <X11/Xlib.h>
#include <gdk/gdkx.h>
#endif

int display_init(display_context_t *ctx) {
    GdkDisplay *gdk_display;

    memset(ctx, 0, sizeof(display_context_t));

    gdk_display = gdk_display_get_default();
    if (!gdk_display) {
        fprintf(stderr, "Failed to get default GDK display\n");
        return -1;
    }

    return display_init_with_gdk_display(ctx, gdk_display);
}

int display_init_with_gdk_display(display_context_t *ctx, void *gdk_display) {
    memset(ctx, 0, sizeof(display_context_t));

    if (!gdk_display) {
        fprintf(stderr, "Failed to get GDK display\n");
        return -1;
    }

    #ifdef WAYLAND_SUPPORT
    // Check if GTK is using Wayland
    if (G_TYPE_CHECK_INSTANCE_TYPE(gdk_display, gdk_wayland_display_get_type())) {
        ctx->backend = DISPLAY_BACKEND_WAYLAND;
        // Don't create our own Wayland connection - use GTK's
        ctx->data.wayland.display = NULL;  // Will be set when needed
        ctx->data.wayland.registry = NULL;
        return 0;
    }
#endif

#ifdef X11_SUPPORT
    // Check if GTK is using X11
    if (GDK_IS_X11_DISPLAY(gdk_display)) {
        fprintf(stderr, "X11 detected\n");
        ctx->backend = DISPLAY_BACKEND_X11;
        // Get the X11 display from GTK
        ctx->data.x11.dpy = gdk_x11_display_get_xdisplay(gdk_display);
        ctx->data.x11.root = (void*)DefaultRootWindow(ctx->data.x11.dpy);
        return 0;
    }
#endif

    return -1; // Unknown display backend
}

void display_cleanup(display_context_t *ctx) {
    // Since we're using GTK's display connections, we don't need to close them
    // Just clear the context
    memset(ctx, 0, sizeof(display_context_t));
}

int display_get_fd(display_context_t *ctx) {
    switch (ctx->backend) {
#ifdef X11_SUPPORT
        case DISPLAY_BACKEND_X11:
            if (ctx->data.x11.dpy) {
                return ConnectionNumber((Display *)ctx->data.x11.dpy);
            }
            break;
#endif
#ifdef WAYLAND_SUPPORT
        case DISPLAY_BACKEND_WAYLAND:
            // For Wayland, let GTK handle the display events
            // Return -1 to indicate we don't have a separate fd
            return -1;
#endif
        default:
            break;
    }
    return -1;
}

int display_dispatch(display_context_t *ctx) {
    switch (ctx->backend) {
#ifdef X11_SUPPORT
        case DISPLAY_BACKEND_X11:
            if (ctx->data.x11.dpy) {
                return XPending((Display *)ctx->data.x11.dpy);
            }
            break;
#endif
#ifdef WAYLAND_SUPPORT
        case DISPLAY_BACKEND_WAYLAND:
            // For Wayland, GTK handles all display dispatching
            // Return 0 to indicate no custom dispatching needed
            return 0;
#endif
        default:
            break;
    }
    return 0;
}