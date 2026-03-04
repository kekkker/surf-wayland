// ============================================================
// display.c - wayland only
// ============================================================

#include "display.h"
#include <gdk/gdk.h>
#include <gdk/gdkwayland.h>
#include <stdio.h>
#include <string.h>

int
display_init(display_context_t *ctx)
{
	GdkDisplay *gdk_display;

	memset(ctx, 0, sizeof(display_context_t));

	gdk_display = gdk_display_get_default();
	if (!gdk_display) {
		fprintf(stderr, "Failed to get default GDK display\n");
		return -1;
	}

	return display_init_with_gdk_display(ctx, gdk_display);
}

int
display_init_with_gdk_display(display_context_t *ctx, void *gdk_display)
{
	memset(ctx, 0, sizeof(display_context_t));

	if (!gdk_display) {
		fprintf(stderr, "Failed to get GDK display\n");
		return -1;
	}

	ctx->backend = DISPLAY_BACKEND_WAYLAND;
	return 0;
}

void
display_cleanup(display_context_t *ctx)
{
	memset(ctx, 0, sizeof(display_context_t));
}
