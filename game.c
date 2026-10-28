#include "world_generation.h"
#include "game.h"
#include <math.h>

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
  return game_init_with_relief(game, config, TERRAIN_RELIEF_ALL);
}

int game_init_with_relief(Game *const game, const GameConfig config, const unsigned int relief) {
  if (!create_terrain_streamed_with_relief(&game->world, config.blocksPerSide, config.center,
                                         config.blockSize, config.seed, relief))
    return 0;
  game->player = (Player){
      .position = {game->world.origin.x + 0.5f * config.blockSize, config.center.y,
                   game->world.origin.z + 0.5f * config.blockSize},
      .eyeHeight = 1.0f, .aim = game_aim(-90.0f, 0.0f)};
  /* Prefer the eastern relief experiment; smaller worlds fall back to beach 01
     or the origin. Resolve actual support for the full player footprint. */
  static const struct { int x, z; float yaw; } spawns[] = {
      {682, 170, 45.0f}, {170, 170, -90.0f}};
  for (size_t i = 0; i < sizeof(spawns) / sizeof(*spawns); i++) {
    const int spawnX = spawns[i].x, spawnZ = spawns[i].z;
    if (spawnX < game->world.minX || spawnX >= game->world.minX + config.blocksPerSide ||
        spawnZ < game->world.minZ || spawnZ >= game->world.minZ + config.blocksPerSide) continue;
    Player landSpawn = game->player;
    landSpawn.position = (Vec3){game->world.origin.x + (float)spawnX * config.blockSize,
                               game->world.origin.y + config.blockSize,
                               game->world.origin.z + (float)spawnZ * config.blockSize};
    const float waterSurface = game->world.origin.y + (float)game->world.waterTopY * config.blockSize;
    if (player_spawn_on_surface(&landSpawn, &game->world) && landSpawn.position.y > waterSurface) {
      landSpawn.aim = game_aim(spawns[i].yaw, 0.0f);
      game->player = landSpawn;
      break;
    }
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
