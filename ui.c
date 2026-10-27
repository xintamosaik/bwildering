#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

enum MenuAction {
  MENU_NONE, MENU_RESUME, MENU_QUIT, MENU_SETTINGS, MENU_BACK,
  MENU_DISTANCE_DOWN, MENU_DISTANCE_UP
};

/* Logical offsets from the menu centre; drawing, hit testing and automation
   all use these controls rather than maintaining separate layouts. */
int ui_menu_buttons(const UiState *const ui, const float viewDistance, UiButton buttons[const UI_BUTTON_CAPACITY]) {
  if (ui->settingsOpen) {
    buttons[0] = (UiButton){.action = MENU_DISTANCE_DOWN, .label = "-", .centerX = -116, .top = -24, .width = 48,
        .enabled = viewDistance > MIN_VIEW_DISTANCE};
    buttons[1] = (UiButton){.action = MENU_DISTANCE_UP, .label = "+", .centerX = 116, .top = -24, .width = 48,
        .enabled = viewDistance < MAX_VIEW_DISTANCE};
    buttons[2] = (UiButton){.action = MENU_BACK, .label = "BACK", .centerX = 0, .top = 40, .width = 280, .enabled = 1};
  } else {
    buttons[0] = (UiButton){.action = MENU_RESUME, .label = "RESUME", .centerX = 0, .top = -88, .width = 280, .enabled = 1};
    buttons[1] = (UiButton){.action = MENU_SETTINGS, .label = "SETTINGS", .centerX = 0, .top = -24, .width = 280, .enabled = 1};
    buttons[2] = (UiButton){.action = MENU_QUIT, .label = "QUIT", .centerX = 0, .top = 40, .width = 280, .enabled = 1};
  }
  return UI_BUTTON_CAPACITY;
}

static int pause_menu_button(const UiState *const ui, const float viewDistance, const double x, const double y,
                              const int width, const int height) {
  const float scale = fminf((float)width / 640.0f, (float)height / 480.0f);
  if (scale <= 0)
    return MENU_NONE;
  const double offset = (y - height * 0.5) / scale;
  const double horizontal = (x - width * 0.5) / scale;
  UiButton buttons[UI_BUTTON_CAPACITY];
  const int count = ui_menu_buttons(ui, viewDistance, buttons);
  for (int i = 0; i < count; i++) {
    const UiButton *const button = &buttons[i];
    if (button->enabled && fabs(horizontal - button->centerX) <= button->width / 2 &&
        offset >= button->top && offset <= button->top + 48)
      return button->action;
  }
  return MENU_NONE;
}

static int pointer_button(const UiState *const ui, const float viewDistance, const UiPointer pointer) {
  if (pointer.windowWidth <= 0 || pointer.windowHeight <= 0)
    return MENU_NONE;
  return pause_menu_button(ui, viewDistance,
      pointer.x * pointer.framebufferWidth / pointer.windowWidth,
      pointer.y * pointer.framebufferHeight / pointer.windowHeight,
      pointer.framebufferWidth, pointer.framebufferHeight);
}

UiAction ui_click(UiState *const ui, const float viewDistance, const UiPointer pointer) {
  const int selected = pointer_button(ui, viewDistance, pointer);
  if (selected == MENU_RESUME)
    return (UiAction){.type = UI_RESUME};
  if (selected == MENU_QUIT)
    return (UiAction){.type = UI_QUIT};
  if (selected == MENU_SETTINGS)
    ui->settingsOpen = 1;
  else if (selected == MENU_BACK)
    ui->settingsOpen = 0;
  else if (selected == MENU_DISTANCE_DOWN || selected == MENU_DISTANCE_UP) {
    const float distance = viewDistance +
        (selected == MENU_DISTANCE_UP ? VIEW_DISTANCE_STEP : -VIEW_DISTANCE_STEP);
    return (UiAction){.type = UI_VIEW_DISTANCE,
        .viewDistance = fmaxf(MIN_VIEW_DISTANCE, fminf(MAX_VIEW_DISTANCE, distance))};
  }
  return (UiAction){0};
}

UiAction ui_handle_pause(UiState *const ui) {
  if (ui->settingsOpen) {
    ui->settingsOpen = 0;
    return (UiAction){0};
  }
  return (UiAction){.type = UI_RESUME};
}

static const unsigned char *font_glyph(const char character) {
  static const unsigned char glyph0[7] = {0x0e, 0x11, 0x13, 0x15,
                                          0x19, 0x11, 0x0e};
  static const unsigned char glyph1[7] = {0x04, 0x0c, 0x04, 0x04,
                                          0x04, 0x04, 0x0e};
  static const unsigned char glyph2[7] = {0x0e, 0x11, 0x01, 0x02,
                                          0x04, 0x08, 0x1f};
  static const unsigned char glyph3[7] = {0x1e, 0x01, 0x01, 0x0e,
                                          0x01, 0x01, 0x1e};
  static const unsigned char glyph4[7] = {0x02, 0x06, 0x0a, 0x12,
                                          0x1f, 0x02, 0x02};
  static const unsigned char glyph5[7] = {0x1f, 0x10, 0x10, 0x1e,
                                          0x01, 0x01, 0x1e};
  static const unsigned char glyph6[7] = {0x0e, 0x10, 0x10, 0x1e,
                                          0x11, 0x11, 0x0e};
  static const unsigned char glyph7[7] = {0x1f, 0x01, 0x02, 0x04,
                                          0x08, 0x08, 0x08};
  static const unsigned char glyph8[7] = {0x0e, 0x11, 0x11, 0x0e,
                                          0x11, 0x11, 0x0e};
  static const unsigned char glyph9[7] = {0x0e, 0x11, 0x11, 0x0f,
                                          0x01, 0x01, 0x0e};
  static const unsigned char glyphF[7] = {0x1f, 0x10, 0x10, 0x1e,
                                          0x10, 0x10, 0x10};
  static const unsigned char glyphP[7] = {0x1e, 0x11, 0x11, 0x1e,
                                          0x10, 0x10, 0x10};
  static const unsigned char glyphS[7] = {0x0f, 0x10, 0x10, 0x0e,
                                          0x01, 0x01, 0x1e};
  static const unsigned char glyphColon[7] = {0x00, 0x04, 0x04, 0x00,
                                              0x04, 0x04, 0x00};
  static const unsigned char glyphA[7] = {14,17,17,31,17,17,17};
  static const unsigned char glyphU[7] = {17,17,17,17,17,17,14};
  static const unsigned char glyphE[7] = {31,16,16,30,16,16,31};
  static const unsigned char glyphD[7] = {30,17,17,17,17,17,30};
  static const unsigned char glyphR[7] = {30,17,17,30,20,18,17};
  static const unsigned char glyphM[7] = {17,27,21,21,17,17,17};
  static const unsigned char glyphQ[7] = {14,17,17,17,21,18,13};
  static const unsigned char glyphI[7] = {14,4,4,4,4,4,14};
  static const unsigned char glyphT[7] = {31,4,4,4,4,4,4};
  static const unsigned char glyphC[7] = {15,16,16,16,16,16,15};
  static const unsigned char glyphO[7] = {14,17,17,17,17,17,14};
  static const unsigned char glyphN[7] = {17,25,25,21,19,19,17};
  static const unsigned char glyphG[7] = {14,17,16,23,17,17,14};
  static const unsigned char glyphV[7] = {17,17,17,17,17,10,4};
  static const unsigned char glyphW[7] = {17,17,17,21,21,27,17};
  static const unsigned char glyphB[7] = {30,17,17,30,17,17,30};
  static const unsigned char glyphK[7] = {17,18,20,24,20,18,17};
  static const unsigned char glyphH[7] = {17,17,17,31,17,17,17};
  static const unsigned char glyphL[7] = {16,16,16,16,16,16,31};
  static const unsigned char glyphX[7] = {17,17,10,4,10,17,17};
  static const unsigned char glyphY[7] = {17,17,10,4,4,4,4};
  static const unsigned char glyphPlus[7] = {0,4,4,31,4,4,0};
  static const unsigned char glyphMinus[7] = {0,0,0,31,0,0,0};

  switch (character) {
  case '0': return glyph0;
  case '1': return glyph1;
  case '2': return glyph2;
  case '3': return glyph3;
  case '4': return glyph4;
  case '5': return glyph5;
  case '6': return glyph6;
  case '7': return glyph7;
  case '8': return glyph8;
  case '9': return glyph9;
  case 'F': return glyphF;
  case 'P': return glyphP;
  case 'S': return glyphS;
  case ':': return glyphColon;
  case 'A': return glyphA;
  case 'U': return glyphU;
  case 'E': return glyphE;
  case 'D': return glyphD;
  case 'R': return glyphR;
  case 'M': return glyphM;
  case 'Q': return glyphQ;
  case 'I': return glyphI;
  case 'T': return glyphT;
  case 'C': return glyphC;
  case 'O': return glyphO;
  case 'N': return glyphN;
  case 'G': return glyphG;
  case 'V': return glyphV;
  case 'W': return glyphW;
  case 'B': return glyphB;
  case 'K': return glyphK;
  case 'H': return glyphH;
  case 'L': return glyphL;
  case 'X': return glyphX;
  case 'Y': return glyphY;
  case '+': return glyphPlus;
  case '-': return glyphMinus;
  default: return NULL;
  }
}

static int build_text_vertices(const char *const text, const float shadowOffset,
                               const int framebufferWidth, const int framebufferHeight,
                               float *const vertices, const float x, const float y, const float scale) {
  int vertexCount = 0;

  for (int characterIndex = 0; text[characterIndex] != '\0';
       characterIndex++) {
    const unsigned char *const glyph = font_glyph(text[characterIndex]);
    if (glyph == NULL)
      continue;
    const float originX = x + ((float)characterIndex * 18.0f + shadowOffset) * scale;
    const float originY = y + shadowOffset * scale;
    for (int row = 0; row < 7; row++) {
      for (int column = 0; column < 5; column++) {
        if ((glyph[row] & (1u << (4 - column))) == 0)
          continue;
        const float pixelSize = 2.0f * scale;
        const float cellSize = 3.0f * scale;
        const float left = originX + (float)column * cellSize;
        const float top = originY + (float)row * cellSize;
        const float right = left + pixelSize;
        const float bottom = top + pixelSize;
        const float quad[6][2] = {{left, top}, {right, top}, {right, bottom},
                                  {left, top}, {right, bottom}, {left, bottom}};
        for (int vertex = 0; vertex < 6; vertex++) {
          vertices[vertexCount * 3] =
              2.0f * quad[vertex][0] / (float)framebufferWidth - 1.0f;
          vertices[vertexCount * 3 + 1] =
              1.0f - 2.0f * quad[vertex][1] / (float)framebufferHeight;
          vertices[vertexCount * 3 + 2] = 0.0f;
          vertexCount++;
        }
      }
    }
  }
  return vertexCount;
}

static void render_fps(const UiDrawContext *const draw, const int fps) {
  if (draw->framebufferWidth <= 0 || draw->framebufferHeight <= 0)
    return;

  char text[16];
  snprintf(text, sizeof(text), "FPS: %d", fps);
  float vertices[16 * 5 * 7 * 6 * 3];
  const UiDrawState state = ui_draw_begin(draw, UI_DRAW_FPS);

  int vertexCount = build_text_vertices(
      text, 1.0f, draw->framebufferWidth, draw->framebufferHeight,
      vertices, 16, 16, 1);
  ui_draw_vertices(draw, vertices, vertexCount, (Vec3){0.02f, 0.03f, 0.02f});

  vertexCount = build_text_vertices(
      text, 0.0f, draw->framebufferWidth, draw->framebufferHeight,
      vertices, 16, 16, 1);
  ui_draw_vertices(draw, vertices, vertexCount, (Vec3){0.95f, 0.98f, 0.92f});

  ui_draw_end(state);
}

static void draw_menu_vertices(const UiDrawContext *const draw, const float *const vertices,
                                const int count, const Vec3 color) {
  ui_draw_vertices(draw, vertices, count, color);
}

static void draw_menu_rectangle(const UiDrawContext *const draw, const float x, const float y,
                                 const float width, const float height, const Vec3 color) {
  const float left = 2 * x / (float)draw->framebufferWidth - 1;
  const float right = 2 * (x + width) / (float)draw->framebufferWidth - 1;
  const float top = 1 - 2 * y / (float)draw->framebufferHeight;
  const float bottom = 1 - 2 * (y + height) / (float)draw->framebufferHeight;
  const float vertices[] = {left, top, 0, right, top, 0, right, bottom, 0,
                       left, top, 0, right, bottom, 0, left, bottom, 0};
  draw_menu_vertices(draw, vertices, 6, color);
}

static void draw_menu_text(const UiDrawContext *const draw, const char *const text,
                            const float centerX, const float y, const float scale, const Vec3 color) {
  float vertices[UI_TEXT_MAX_CHARS * 5 * 7 * 6 * 3];
  const float width = ((float)strlen(text) * 18 - 4) * scale;
  const int count = build_text_vertices(text, 0, draw->framebufferWidth,
                                  draw->framebufferHeight, vertices,
                                  centerX - width / 2, y, scale);
  draw_menu_vertices(draw, vertices, count, color);
}

static void render_performance(const UiFrame *const a, const UiDrawContext *const draw) {
  const Performance *const p = a->performance;
  if (!p->overlay) return;
  char lines[5][96];
  snprintf(lines[0], 96, "FRAME US AVG %.0f P95 %.0f MAX %.0f", p->average*1e6, p->p95*1e6, p->maximum*1e6);
  snprintf(lines[1], 96, "CHUNKS %zu SECTIONS %zu EDITED %zu DIRTY %zu", a->residentChunks, a->sections, a->editedChunks, a->dirtySections);
  snprintf(lines[2], 96, "REBUILDS %zu UPLOAD BYTES %zu", p->total.rebuilds, p->total.uploadBytes);
  snprintf(lines[3], 96, "DRAWS %zu TRIANGLES %zu", p->frame.draws, p->frame.triangles);
  snprintf(lines[4], 96, "NEIGHBORHOOD SLOTS %zu GPU REQUESTED BYTES %zu", a->neighborhoodSlots, a->gpuBytes);
  const UiDrawState state = ui_draw_begin(draw, UI_DRAW_OVERLAY);
  for (int i = 0; i < 5; i++)
    draw_menu_text(draw, lines[i], (float)draw->framebufferWidth * 0.5f,
                   48.0f + (float)i * 18.0f, 0.6f, (Vec3){.x = 0.95f, .y = 0.98f, .z = 0.92f});
  ui_draw_end(state);
}

static void draw_menu_button(const UiDrawContext *const draw, const char *const label,
                              const float centerX, const float top, const float width, const float scale,
                              const int hovered, const int enabled) {
  const Vec3 color = !enabled ? (Vec3){.x = 0.15f, .y = 0.18f, .z = 0.15f} :
      hovered ? (Vec3){.x = 0.46f, .y = 0.58f, .z = 0.38f} : (Vec3){.x = 0.25f, .y = 0.32f, .z = 0.24f};
  draw_menu_rectangle(draw, centerX - width * scale / 2, top,
                       width * scale, 48 * scale, color);
  draw_menu_text(draw, label, centerX, top + 14 * scale, scale,
                   enabled ? (Vec3){.x = 0.95f, .y = 0.98f, .z = 0.92f} : (Vec3){.x = 0.4f, .y = 0.45f, .z = 0.4f});
}

static void render_pause_menu(const UiState *const ui, const UiFrame *const frame, const UiDrawContext *const draw) {
  const int width = draw->framebufferWidth;
  const int height = draw->framebufferHeight;
  if (width <= 0 || height <= 0)
    return;
  const float scale = fminf((float)width / 640.0f, (float)height / 480.0f);
  const float centerX = (float)width * 0.5f;
  const float centerY = (float)height * 0.5f;
  const float viewDistance = frame->viewDistance;
  const int hovered = pointer_button(ui, viewDistance, frame->pointer);
  const UiDrawState state = ui_draw_begin(draw, UI_DRAW_MENU);
  draw_menu_rectangle(draw, centerX - 180 * scale, centerY - 160 * scale,
                       360 * scale, 300 * scale, (Vec3){.x = 0.09f, .y = 0.12f, .z = 0.10f});
  draw_menu_text(draw, ui->settingsOpen ? "SETTINGS" : "PAUSED",
                 centerX, centerY - 130 * scale, scale * 1.2f,
                 (Vec3){.x = 0.95f, .y = 0.98f, .z = 0.92f});
  if (ui->settingsOpen) {
    char value[16];
    snprintf(value, sizeof(value), "%.0f UNITS", viewDistance);
    draw_menu_text(draw, "VIEW DISTANCE", centerX, centerY - 65 * scale,
                   scale, (Vec3){.x = 0.95f, .y = 0.98f, .z = 0.92f});
    draw_menu_text(draw, value, centerX, centerY - 9 * scale,
                   scale * 0.85f, (Vec3){.x = 0.95f, .y = 0.98f, .z = 0.92f});
  }
  UiButton buttons[UI_BUTTON_CAPACITY];
  const int count = ui_menu_buttons(ui, viewDistance, buttons);
  for (int i = 0; i < count; i++) {
    const UiButton *const button = &buttons[i];
    draw_menu_button(draw, button->label, centerX + button->centerX * scale,
                       centerY + button->top * scale, button->width, scale,
                       hovered == button->action, button->enabled);
  }
  draw_menu_text(draw, ui->settingsOpen ? "ESC TO PAUSE" : "ESC TO RESUME",
                 centerX, centerY + 110 * scale,
                 scale * 0.7f, (Vec3){.x = 0.65f, .y = 0.73f, .z = 0.62f});
  ui_draw_end(state);
}

void ui_render(const UiState *const ui, const UiFrame *const frame, const UiDrawContext *const draw) {
  render_fps(draw, frame->fps);
  render_performance(frame, draw);
  if (frame->paused)
    render_pause_menu(ui, frame, draw);
}
