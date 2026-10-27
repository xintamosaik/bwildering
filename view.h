#ifndef VIEW_H
#define VIEW_H

#include "game_logic.h"

/* Client presentation value, derived without modifying authoritative state. */
typedef struct {
  Vec3 eye, front, up;
} View;

static inline View player_view(const Player *const player, const GameplayAim aim) {
  return (View){player_eye_position(player), aim.front, {0.0f, 1.0f, 0.0f}};
}

static inline View player_committed_view(const Player *const player) {
  return player_view(player, player->aim);
}

#endif
