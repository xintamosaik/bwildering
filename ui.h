#ifndef UI_H
#define UI_H

#include "performance.h"

/* Shared text capacity for the renderer buffer and UI geometry. */
enum { UI_TEXT_MAX_CHARS = 96 };

static const float MIN_VIEW_DISTANCE = 25.0f;
static const float MAX_VIEW_DISTANCE = 300.0f;
static const float VIEW_DISTANCE_STEP = 25.0f;

/* Called only for a left click while paused. */
void ui_click(Application *application);
/* Returns nonzero when a new Escape press changes the menu state. */
int ui_handle_escape(Application *application);
void ui_render(const Application *application);

#endif
