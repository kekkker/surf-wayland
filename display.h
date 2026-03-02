#ifndef DISPLAY_H
#define DISPLAY_H

typedef enum {
	DISPLAY_BACKEND_WAYLAND
} display_backend_t;

typedef struct {
	display_backend_t backend;
} display_context_t;

int display_init(display_context_t *ctx);
int display_init_with_gdk_display(display_context_t *ctx, void *gdk_display);
void display_cleanup(display_context_t *ctx);

#endif
