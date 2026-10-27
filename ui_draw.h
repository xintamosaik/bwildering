#ifndef UI_DRAW_H
#define UI_DRAW_H

#include "game_logic.h"

/* Borrowed only during ui_render in the current frame. Pixel coordinates use
   the framebuffer size; vertices are clip-space xyz triangles. The fixed buffer
   holds at most UI_TEXT_MAX_CHARS glyphs (5*7 cells, 6 vertices each). */
enum { UI_TEXT_MAX_CHARS = 96 };
typedef struct {
  const void *owner;
  int framebufferWidth, framebufferHeight;
} UiDrawContext;
typedef enum { UI_DRAW_FPS, UI_DRAW_OVERLAY, UI_DRAW_MENU } UiDrawMode;
typedef struct { int depthEnabled, cullEnabled, menu; } UiDrawState;

/* FPS/overlay restore depth testing to enabled, as before. Menu drawing saves
   and restores depth/cull enables. FPS/menu bind the UI buffer and identity
   matrix; the overlay continues the FPS binding, preserving submission order. */
UiDrawState ui_draw_begin(const UiDrawContext *draw, UiDrawMode mode);
void ui_draw_vertices(const UiDrawContext *draw, const float *vertices, int count, Vec3 color);
void ui_draw_end(UiDrawState state);

#endif
