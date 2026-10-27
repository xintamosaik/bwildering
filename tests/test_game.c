#include "../game.h"
#include "../view.h"
#include "../storage.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static int reject_world(const StorageCategory category) {
  return category == STORAGE_WORLD;
}

static void test_lifetime_and_spawn(void) {
  const GameConfig configs[] = {
      {128, {0.0f, 0.0f, 0.0f}, 1.0f, UINT32_C(0x4B1D2A37)},
      {16, {12.0f, -3.0f, 8.0f}, 0.5f, 1},
      {1, {0.0f, 0.0f, 0.0f}, 1.0f, 42},
  };
  for (size_t i = 0; i < sizeof(configs) / sizeof(*configs); i++) {
    Game game = {0};
    assert(game_init(&game, configs[i]));
    const World *const world = game_world(&game);
    const Player *const player = game_player(&game);
    assert(world->terrainSeed == configs[i].seed);
    assert(world->blocksPerSide == configs[i].blocksPerSide);
    assert(world->blockSize == configs[i].blockSize);
    assert(player->grounded && game.tick == 0);
    const Vec3 spawn = player->position;
    assert(isfinite(spawn.y));
    for (int tick = 0; tick < 60; tick++)
      game_step(&game, (GameCommand){.yaw = -90.0f});
    assert(game.tick == 60 && player->grounded);
    assert(fabsf(player->position.y - spawn.y) < 0.001f);
    game_step(&game, (GameCommand){.yaw = -90.0f, .movement.jump = 1});
    assert(!player->grounded && player->position.y > spawn.y);
    game_destroy(&game);
    assert(!game.world.storage && !game.world.chunks && game.tick == 0);
    game_destroy(&game);
  }
  Game game = {0};
  assert(!game_init(&game, (GameConfig){0}));
  storage_reject = reject_world;
  assert(!game_init(&game, configs[0]));
  storage_reject = NULL;
  assert(!game.world.storage && !game.world.chunks);
  game_destroy(&game);
}

/* Controlled fixture terrain; all subsequent gameplay enters through commands. */
static void setup_flat(Game *const game) {
  assert(world_init_region(&game->world, -8, -8, 16, (Vec3){0}, 1.0f));
  for (int x = -8; x < 8; x++)
    for (int z = -8; z < 8; z++)
      assert(world_set_block(&game->world, x, -1, z, BLOCK_TYPE_STONE));
  game->player = (Player){.position = {0.5f, 0.0f, 0.5f},
      .eyeHeight = 1.0f, .grounded = 1, .aim = game_aim(-90.0f, 0.0f)};
}

static void test_commands_and_view(void) {
  Game game = {0};
  setup_flat(&game);
  const Player *const player = game_player(&game);
  const View pending = player_view(player, game_aim(0.0f, 0.0f));
  assert(pending.front.x > 0.99f && player->aim.front.z < -0.99f);
  game_step(&game, (GameCommand){.yaw = 0.0f, .movement.forward = 1.0f});
  assert(player->position.x > 0.5f && player->position.z == 0.5f);
  assert(game.tick == 1 && player->aim.front.x > 0.99f);
  assert(world_set_block(&game.world, 3, 1, 0, BLOCK_TYPE_GRASS));
  game_step(&game, (GameCommand){.yaw = 0.0f, .place = 1, .material = BLOCK_TYPE_SAND});
  assert(world_get_block(game_world(&game), 2, 1, 0) == BLOCK_TYPE_SAND);
  game_step(&game, (GameCommand){.yaw = 0.0f, .remove = 1});
  assert(world_get_block(game_world(&game), 2, 1, 0) == BLOCK_TYPE_EMPTY);
  game_step(&game, (GameCommand){.yaw = 0.0f, .place = 1, .remove = 1,
                                .material = BLOCK_TYPE_SNOW});
  assert(world_get_block(game_world(&game), 2, 1, 0) == BLOCK_TYPE_EMPTY);
  assert(world_get_block(game_world(&game), 3, 1, 0) == BLOCK_TYPE_GRASS);
  game_step(&game, (GameCommand){.yaw = 0.0f, .place = 1, .material = BLOCK_TYPE_EMPTY});
  assert(world_get_block(game_world(&game), 2, 1, 0) == BLOCK_TYPE_EMPTY);
  game_step(&game, (GameCommand){.yaw = 180.0f, .remove = 1});
  assert(world_get_block(game_world(&game), 3, 1, 0) == BLOCK_TYPE_GRASS);
  assert(world_set_block(&game.world, 3, 1, 0, BLOCK_TYPE_EMPTY));
  assert(world_set_block(&game.world, 7, 1, 0, BLOCK_TYPE_STONE));
  game_step(&game, (GameCommand){.yaw = 0.0f, .remove = 1});
  assert(world_get_block(game_world(&game), 7, 1, 0) == BLOCK_TYPE_STONE);
  game_step(&game, (GameCommand){.yaw = NAN, .pitch = INFINITY,
                                .movement = {.forward = NAN, .strafe = INFINITY, .crouch = 1}});
  assert(isfinite(player->position.x) && isfinite(player->aim.front.x));
  assert(player->crouching);
  const View committed = player_committed_view(player);
  assert(committed.eye.y == player_eye_position(player).y);
  assert(game_aim(0.0f, 100.0f).pitch == 89.0f);
  game_destroy(&game);
}

int main(void) {
  test_lifetime_and_spawn();
  test_commands_and_view();
  puts("Headless game ownership tests passed.");
  return 0;
}
