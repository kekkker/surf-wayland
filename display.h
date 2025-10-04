#ifndef DISPLAY_H
#define DISPLAY_H

typedef enum {
    DISPLAY_BACKEND_X11,
    DISPLAY_BACKEND_WAYLAND
} display_backend_t;

typedef struct {
    display_backend_t backend;
    union {
        struct {
            void *dpy;        // Display * for X11, cast to void* to avoid X11 headers
            void *root;       // Window for X11, cast to void* to avoid X11 headers
        } x11;
        struct {
            void *display;    // struct wl_display *, cast to void* to avoid Wayland headers
            void *registry;   // struct wl_registry *, cast to void* to avoid Wayland headers
        } wayland;
    } data;
} display_context_t;

// Display management functions
int display_init(display_context_t *ctx);
int display_init_with_gdk_display(display_context_t *ctx, void *gdk_display);
void display_cleanup(display_context_t *ctx);
int display_get_fd(display_context_t *ctx);
int display_dispatch(display_context_t *ctx);

#endif