#ifndef RENDERER_H
#define RENDERER_H

#include "view.h"
#include "world_streaming.h"
#include "performance.h"
#include "ui_draw.h"

typedef struct Renderer Renderer;

/* Destroy GPU resources while the owning GL context is current. */
int renderer_init(Renderer *renderer);
void renderer_destroy(Renderer *renderer);
void renderer_set_viewport(Renderer *renderer, int width, int height);
float renderer_view_distance(const Renderer *renderer);
void renderer_set_view_distance(Renderer *renderer, float distance);

/* GPU cache adapter; the application owns the streaming coordinator. */
StreamConsumer renderer_stream_consumer(Renderer *renderer);
void render_world(Renderer *renderer, View camera);
size_t renderer_neighborhood_slots(const Renderer *renderer);
UiDrawContext renderer_ui_context(const Renderer *renderer);

#endif
