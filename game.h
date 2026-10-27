#ifndef GAME_H
#define GAME_H

#include "game_logic.h"

#define GAME_TICK_SECONDS (1.0f / 60.0f)

typedef struct {
  int blocksPerSide;
  Vec3 center;
  float blockSize;
  uint32_t seed;
} GameConfig;

/* Owning state: zero initialize, destroy explicitly, never copy a live Game. */
typedef struct {
  World world;
  Player player;
  uint64_t tick;
} Game;

/* Values for one tick. Aim is in degrees; no device or presentation state.
   The caller assigns one-shot actions to a tick and supplies held movement. */
typedef struct {
  PlayerInput movement;
  float yaw, pitch;
  int place, remove;
  BlockType material;
} GameCommand;

/* Initializes a zeroed/destroyed owner; failure leaves it empty and destroyable. */
int game_init(Game *game, GameConfig config);
void game_destroy(Game *game);
/* Order: aim, movement/collision, placement, removal, tick increment. */
void game_step(Game *game, GameCommand command);
GameplayAim game_aim(float yaw, float pitch);
const Player *game_player(const Game *game);
const World *game_world(const Game *game);
/* Borrow only for residency/pool/mesh bookkeeping, never logical edits.
   Do not retain section pointers across cache eviction. Stage 5 extracts policy. */
World *game_streaming_world(Game *game);

#endif
