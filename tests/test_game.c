#include "../world_generation.h"
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
    assert(world_get_block(world, 0, 0, 0) == BLOCK_TYPE_WATER);
    assert(!block_is_solid(BLOCK_TYPE_WATER));
    assert(world_support_height(world, world->origin.x, world->origin.z) ==
           configs[i].center.y + (float)TERRAIN_STONE_TOP_Y * configs[i].blockSize);
    assert(!player->grounded && game.tick == 0);
    const Vec3 spawn = player->position;
    assert(isfinite(spawn.y) && spawn.y == configs[i].center.y);
    for (int tick = 0; tick < 600; tick++)
      game_step(&game, (GameCommand){.yaw = -90.0f});
    const float waterY = configs[i].center.y + (float)(TERRAIN_SEA_LEVEL + 1) * configs[i].blockSize;
    assert(!player->grounded && player->position.y > waterY - 1.0f && player->position.y < waterY);
    assert(fabsf(player->velocityY) < 0.01f);
    const float floatingY = player->position.y;
    for (int tick = 0; tick < 60; tick++)
      game_step(&game, (GameCommand){.yaw = -90.0f, .movement.swimUp = 1});
    assert(!player->grounded && player->position.y > floatingY);
    for (int tick = 0; tick < 600; tick++)
      game_step(&game, (GameCommand){.yaw = -90.0f});
    assert(fabsf(player->position.y - floatingY) < 0.04f);
    const float initialZ = player->position.z;
    for (int tick = 0; tick < 10; tick++)
      game_step(&game, (GameCommand){.yaw = -90.0f, .movement.forward = 1.0f});
    assert(player->position.z < initialZ);
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

static void setup_pool(Game *const game) {
  setup_flat(game);
  for (int x = -8; x < 8; x++)
    for (int y = 0; y < 4; y++)
      for (int z = -8; z < 8; z++)
        assert(world_set_block(&game->world, x, y, z, BLOCK_TYPE_WATER));
  game->player.position.y = 1.0f;
  game->player.grounded = 0;
}

static void test_water_movement(void) {
  Game wet = {0}, dry = {0};
  setup_pool(&wet);
  setup_flat(&dry);
  const float startZ = wet.player.position.z;
  for (int tick = 0; tick < 60; tick++) {
    const GameCommand command = {.yaw = -90.0f, .movement.forward = 1.0f};
    game_step(&wet, command);
    game_step(&dry, command);
  }
  assert(startZ - wet.player.position.z > 1.5f);
  assert(startZ - wet.player.position.z < (startZ - dry.player.position.z) * 0.65f);

  wet.player = (Player){.position = {0.5f, 2.0f, 0.5f}, .eyeHeight = 1.0f};
  for (int tick = 0; tick < 30; tick++)
    game_step(&wet, (GameCommand){.yaw = -90.0f, .pitch = -60.0f, .movement.forward = 1.0f});
  assert(wet.player.position.y < 1.8f && wet.player.position.z < 0.3f);

  wet.player = (Player){.position = {0.5f, 1.0f, 0.5f}, .eyeHeight = 1.0f};
  for (int tick = 0; tick < 60; tick++)
    game_step(&wet, (GameCommand){.yaw = -90.0f, .movement.swimUp = 1});
  assert(wet.player.position.y > 2.5f);
  for (int tick = 0; tick < 120; tick++)
    game_step(&wet, (GameCommand){.yaw = -90.0f, .movement.crouch = 1});
  assert(wet.player.position.y < 0.01f && wet.player.position.y >= -0.001f);
  assert(wet.player.grounded && !wet.player.crouching);
  game_step(&wet, (GameCommand){.yaw = -90.0f, .movement.swimUp = 1});
  assert(!wet.player.grounded && wet.player.position.y > 0.0f);

  wet.player.position.y = 5.0f;
  wet.player.velocityY = -20.0f;
  for (int tick = 0; tick < 10; tick++)
    game_step(&wet, (GameCommand){.yaw = -90.0f});
  assert(wet.player.velocityY > -5.0f && wet.player.position.y > 2.5f);
  wet.player.position.y = 1.0f;
  wet.player.velocityY = 0.0f;

  /* Solid ceilings still stop swimming, and holding Space in air cannot fly. */
  for (int x = -8; x < 8; x++)
    for (int z = -8; z < 8; z++)
      assert(world_set_block(&wet.world, x, 3, z, BLOCK_TYPE_STONE));
  for (int tick = 0; tick < 120; tick++)
    game_step(&wet, (GameCommand){.yaw = -90.0f, .movement.swimUp = 1});
  assert(wet.player.position.y <= 1.501f);
  wet.player.position.y = 5.0f;
  wet.player.velocityY = 0.0f;
  game_step(&wet, (GameCommand){.yaw = -90.0f, .movement.swimUp = 1});
  assert(wet.player.position.y < 5.0f && wet.player.velocityY < 0.0f);
  game_destroy(&wet);
  game_destroy(&dry);
}

static void test_water_float_at_different_rates(void) {
  Game game = {0};
  setup_pool(&game);
  const int rates[] = {30, 60, 144};
  float previousY = 0.0f;
  for (size_t rate = 0; rate < sizeof(rates) / sizeof(*rates); rate++) {
    Player player = {.position = {-0.1f, 1.0f, -0.1f}, .velocityY = -20.0f,
                     .eyeHeight = 1.0f, .aim = game_aim(-90.0f, 0.0f)};
    for (int tick = 0; tick < rates[rate] * 12; tick++)
      update_player(&player, (PlayerInput){0}, 1.0f / (float)rates[rate], &game.world);
    assert(!player.grounded && player.position.y > 3.0f && player.position.y < 4.0f);
    assert(player_eye_position(&player).y > 4.0f && fabsf(player.velocityY) < 0.01f);
    if (rate) assert(fabsf(player.position.y - previousY) < 0.03f);
    previousY = player.position.y;
  }
  Player longFrame = {.position = {0.5f, 1.0f, 0.5f}, .eyeHeight = 1.0f,
                      .aim = game_aim(-90.0f, 0.0f)};
  Player shortFrames = longFrame;
  update_player(&longFrame, (PlayerInput){.swimUp = 1}, 2.0f, &game.world);
  for (int tick = 0; tick < 120; tick++)
    update_player(&shortFrames, (PlayerInput){.swimUp = 1}, GAME_TICK_SECONDS, &game.world);
  assert(longFrame.position.y < 4.5f);
  assert(fabsf(longFrame.position.y - shortFrames.position.y) < 0.03f);
  game_destroy(&game);
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

/* Preassigned commands are independent of when a renderer asks for ticks.
   Dyadic frame durations avoid ambiguous tick boundaries from decimal clocks. */
static void test_replay_schedules(void) {
  enum { COMMAND_COUNT = 120 };
  GameCommand commands[COMMAND_COUNT] = {0};
  for (int tick = 0; tick < COMMAND_COUNT; tick++)
    commands[tick] = (GameCommand){
        .yaw = tick < 60 ? 0.0f : 180.0f,
        .movement = {.forward = tick < 90 ? 1.0f : 0.0f,
                     .jump = tick == 20 || tick == 95,
                     .run = tick < 30, .crouch = tick >= 60 && tick < 90},
        .place = tick == 0 || tick == 2, .remove = tick == 1 || tick == 2,
        .material = BLOCK_TYPE_SNOW};
  Game reference = {0};
  setup_flat(&reference);
  assert(world_set_block(&reference.world, 3, 1, 0, BLOCK_TYPE_GRASS));
  for (int tick = 0; tick < COMMAND_COUNT; tick++)
    game_step(&reference, commands[tick]);
  const double schedules[][4] = {
      {1.0 / 256.0, 1.0 / 256.0, 1.0 / 256.0, 1.0 / 256.0},
      {1.0 / 8.0, 0.0, 1.0 / 64.0, 1.0 / 32.0},
  };
  for (size_t schedule = 0; schedule < sizeof(schedules) / sizeof(*schedules); schedule++) {
    Game replay = {0};
    setup_flat(&replay);
    assert(world_set_block(&replay.world, 3, 1, 0, BLOCK_TYPE_GRASS));
    double accumulator = 0.0;
    int next = 0;
    for (size_t frame = 0; next < COMMAND_COUNT; frame++) {
      accumulator += schedules[schedule][frame % 4];
      while (accumulator >= 1.0 / 60.0 && next < COMMAND_COUNT) {
        game_step(&replay, commands[next++]);
        accumulator -= 1.0 / 60.0;
      }
    }
    assert(replay.tick == reference.tick);
    const Player *const a = &reference.player;
    const Player *const b = &replay.player;
    /* Same operation order should be exact; 1e-6 world units also allows
       harmless float rounding far below collision's 0.001 separation. */
    assert(fabsf(a->position.x - b->position.x) < 1e-6f);
    assert(fabsf(a->position.y - b->position.y) < 1e-6f);
    assert(fabsf(a->position.z - b->position.z) < 1e-6f);
    assert(fabsf(a->velocityY - b->velocityY) < 1e-6f);
    assert(a->grounded == b->grounded && a->crouching == b->crouching);
    assert(a->eyeHeight == b->eyeHeight);
    assert(a->aim.yaw == b->aim.yaw && a->aim.pitch == b->aim.pitch);
    assert(reference.world.voxelCount == replay.world.voxelCount);
    for (int x = -8; x < 8; x++)
      for (int z = -8; z < 8; z++)
        for (int y = -1; y <= 4; y++)
          assert(world_get_block(&reference.world, x, y, z) ==
                 world_get_block(&replay.world, x, y, z));
    game_destroy(&replay);
  }
  game_destroy(&reference);
}

static void test_movement_before_edit(void) {
  Game game = {0};
  setup_flat(&game);
  game.player.position.x = 0.95f;
  assert(world_set_block(&game.world, 7, 1, 0, BLOCK_TYPE_GRASS));
  game_step(&game, (GameCommand){.yaw = 0.0f, .remove = 1});
  assert(world_get_block(&game.world, 7, 1, 0) == BLOCK_TYPE_GRASS);
  /* Movement crosses the six-unit reach boundary during this tick. Both the
     movement and targeting must use this command's aim, not the previous aim. */
  game.player.aim = game_aim(-90.0f, 0.0f);
  game_step(&game, (GameCommand){.yaw = 0.0f, .movement.forward = 1.0f, .remove = 1});
  assert(game.player.position.x > 1.0f && game.tick == 2);
  assert(world_get_block(&game.world, 7, 1, 0) == BLOCK_TYPE_EMPTY);
  game_destroy(&game);
}

int main(void) {
  test_movement_before_edit();
  test_replay_schedules();
  test_lifetime_and_spawn();
  test_water_movement();
  test_water_float_at_different_rates();
  test_commands_and_view();
  puts("Headless game ownership tests passed.");
  return 0;
}
