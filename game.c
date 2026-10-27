#include "game.h"
#include <math.h>
#include <stdlib.h>

GameplayAim game_aim(const float yaw, const float pitch) {
  const float boundedYaw = isfinite(yaw) ? remainderf(yaw, 360.0f) : -90.0f;
  const float boundedPitch = isfinite(pitch) ? fmaxf(-89.0f, fminf(89.0f, pitch)) : 0.0f;
  const float radians = 3.14159265f / 180.0f;
  return (GameplayAim){.yaw = boundedYaw, .pitch = boundedPitch,
      .front = vec3_normalize((Vec3){cosf(boundedYaw * radians) * cosf(boundedPitch * radians),
                                    sinf(boundedPitch * radians),
                                    sinf(boundedYaw * radians) * cosf(boundedPitch * radians)})};
}

int game_init(Game *const game, const GameConfig config) {
  if (!create_terrain_streamed(&game->world, config.blocksPerSide, config.center,
                                config.blockSize, config.seed))
    return 0;
  game->player = (Player){
      .position = {game->world.origin.x + 0.5f * config.blockSize, config.center.y,
                   game->world.origin.z + 0.5f * config.blockSize},
      .grounded = 1, .eyeHeight = 1.0f, .aim = game_aim(-90.0f, 0.0f)};
  /* Bounded deterministic search: prefer a grassy lowland near the start. */
  for (int ring = 0, found = 0; ring <= 64 && !found; ring++)
    for (int dx = -ring; dx <= ring && !found; dx++)
      for (int dz = -ring; dz <= ring && !found; dz++) {
        if (abs(dx) != ring && abs(dz) != ring) continue;
        const int x = (int)floorf((game->player.position.x - game->world.origin.x) / config.blockSize) + dx * SECTION_SIZE;
        const int z = (int)floorf((game->player.position.z - game->world.origin.z) / config.blockSize) + dz * SECTION_SIZE;
        if (x < game->world.minX || x >= game->world.minX + config.blocksPerSide ||
            z < game->world.minZ || z >= game->world.minZ + config.blocksPerSide) continue;
        const TerrainColumn c = terrain_sample_column(game->world.terrainSeed, x, z);
        if (c.material != BLOCK_TYPE_GRASS || c.height < 8 || c.height > 80) continue;
        game->player.position.x = game->world.origin.x + ((float)x + 0.5f) * config.blockSize;
        game->player.position.z = game->world.origin.z + ((float)z + 0.5f) * config.blockSize;
        found = 1;
      }
  if (!player_spawn_on_surface(&game->player, &game->world)) {
    game_destroy(game);
    return 0;
  }
  game->tick = 0;
  return 1;
}

void game_destroy(Game *const game) {
  world_destroy(&game->world);
  *game = (Game){0};
}

static float movement_axis(const float value) {
  return isfinite(value) ? fmaxf(-1.0f, fminf(1.0f, value)) : 0.0f;
}

void game_step(Game *const game, const GameCommand command) {
  game->player.aim = game_aim(command.yaw, command.pitch);
  PlayerInput movement = command.movement;
  movement.forward = movement_axis(movement.forward);
  movement.strafe = movement_axis(movement.strafe);
  update_player(&game->player, movement, GAME_TICK_SECONDS, &game->world);
  /* Reach is gameplay policy in world units; callers cannot extend it. */
  if (command.place)
    player_place_block(&game->world, &game->player, command.material, 6.0f);
  if (command.remove)
    player_remove_block(&game->world, &game->player, 6.0f);
  game->tick++;
}

const Player *game_player(const Game *const game) { return &game->player; }
const World *game_world(const Game *const game) { return &game->world; }
World *game_streaming_world(Game *const game) { return &game->world; }
