#ifndef RENDERER_H
#define RENDERER_H

#include "game_logic.h"
#include "performance.h"
#include "ui_draw.h"

typedef struct Renderer Renderer;
typedef enum { STREAM_FAILED = 0, STREAM_COMPLETE = 1, STREAM_DEFERRED = 2 } StreamResult;

/* Destroy GPU resources while the owning GL context is current. */
int renderer_init(Renderer *renderer);
void renderer_destroy(Renderer *renderer);
void renderer_set_viewport(Renderer *renderer, int width, int height);
float renderer_view_distance(const Renderer *renderer);
void renderer_set_view_distance(Renderer *renderer, float distance);

/* Temporary stage-1 coupling: renderer synchronization configures and mutates
   CPU residency and mesh-sync metadata. World is intentionally mutable here;
   the streaming coordinator takes this responsibility in stage 5. */
int renderer_storage_limits(World *world);
int renderer_sync_view(Renderer *renderer, World *world, const Player *player);
StreamResult renderer_step_view(Renderer *renderer, World *world, const Player *player);
void render_world(Renderer *renderer, const Player *player);
size_t renderer_neighborhood_slots(const Renderer *renderer);
UiDrawContext renderer_ui_context(const Renderer *renderer);

#endif
