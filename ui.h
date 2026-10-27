#ifndef UI_H
#define UI_H

#include "performance.h"

#include "ui_draw.h"
#include "view_settings.h"

/* Called only for a left click while paused. */
void ui_click(Application *application);
/* Returns nonzero when a new pause action press changes the menu state. */
int ui_handle_pause(Application *application);
void ui_render(const Application *application, const UiDrawContext *draw);

#endif
