#ifndef UI_H
#define UI_H

#include "performance.h"
#include "ui_draw.h"
#include "view_settings.h"

typedef struct { int settingsOpen; } UiState;
typedef enum { UI_NONE, UI_RESUME, UI_QUIT, UI_VIEW_DISTANCE } UiActionType;
typedef struct { UiActionType type; float viewDistance; } UiAction;
/* Positions are window coordinates; dimensions explicitly map to framebuffer pixels. */
typedef struct {
  double x, y;
  int windowWidth, windowHeight, framebufferWidth, framebufferHeight;
} UiPointer;
/* Read-only frame snapshot, borrowed only for drawing. */
typedef struct {
  int fps, paused;
  Vec3 playerPosition;
  float viewDistance;
  const Performance *performance;
  size_t residentChunks, sections, editedChunks, dirtySections, neighborhoodSlots, gpuBytes;
  UiPointer pointer;
} UiFrame;

/* Borrowed labels and logical offsets from the menu centre. The opaque control
   identifier is shared by hit testing and drawing. No allocation. */
enum { UI_BUTTON_CAPACITY = 3 };
typedef struct {
  int action;
  const char *label;
  float centerX, top, width;
  int enabled;
} UiButton;
int ui_menu_buttons(const UiState *ui, float viewDistance, UiButton buttons[UI_BUTTON_CAPACITY]);

UiAction ui_click(UiState *ui, float viewDistance, UiPointer pointer);
UiAction ui_handle_pause(UiState *ui);
void ui_render(const UiState *ui, const UiFrame *frame, const UiDrawContext *draw);

#endif
