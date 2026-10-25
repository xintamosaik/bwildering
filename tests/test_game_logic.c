#include "../game_logic.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void assert_near(float actual, float expected) {
  assert(fabsf(actual - expected) < 0.0001f);
}

static World single_block_world(void) {
  World world = {0};
  assert(create_platform(&world, 1, (Vec3){0.0f, 0.0f, 0.0f}, 1.0f));
  return world;
}

static Player make_player(Vec3 position, int grounded) {
  return (Player){
      .position = position,
      .grounded = grounded,
      .camera = {
          .front = {0.0f, 0.0f, -1.0f},
          .up = {0.0f, 1.0f, 0.0f},
      },
  };
}

static void test_vector_normalization(void) {
  Vec3 normalized = vec3_normalize((Vec3){3.0f, 0.0f, 4.0f});
  assert_near(normalized.x, 0.6f);
  assert_near(normalized.y, 0.0f);
  assert_near(normalized.z, 0.8f);

  Vec3 zero = vec3_normalize((Vec3){0.0f, 0.0f, 0.0f});
  assert_near(zero.x, 0.0f);
  assert_near(zero.y, 0.0f);
  assert_near(zero.z, 0.0f);
}

static void test_matrix_multiply_identity(void) {
  mat4 identity;
  mat4 original = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f,
                   9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f};
  mat4 result;
  mat4_identity(identity);

  mat4_multiply(identity, original, result);
  for (int i = 0; i < 16; i++)
    assert_near(result[i], original[i]);

  mat4_multiply(original, identity, result);
  for (int i = 0; i < 16; i++)
    assert_near(result[i], original[i]);
}

static void test_diagonal_movement_is_normalized(void) {
  World world = {0};
  Player forward = make_player((Vec3){0.0f, 10.0f, 0.0f}, 0);
  Player diagonal = forward;
  update_player(&forward, (PlayerInput){.forward = 1.0f}, 0.25f, &world);
  update_player(&diagonal,
                (PlayerInput){.forward = 1.0f, .strafe = 1.0f}, 0.25f,
                &world);

  float forwardDistance = fabsf(forward.position.z);
  float diagonalDistance = hypotf(diagonal.position.x,
                                  diagonal.position.z);
  assert_near(forwardDistance, 1.0f);
  assert_near(diagonalDistance, forwardDistance);
}

static void test_landing_on_block(void) {
  World world = single_block_world();
  Player player = make_player((Vec3){0.0f, 0.1f, 0.0f}, 0);
  player.velocityY = -1.0f;

  update_player(&player, (PlayerInput){0}, 0.1f, &world);

  assert(player.grounded);
  assert_near(player.position.y, 0.0f);
  assert_near(player.velocityY, 0.0f);
}

static void test_falling_off_platform_edge(void) {
  World world = single_block_world();
  Player player = make_player((Vec3){0.0f, 0.0f, 0.0f}, 1);

  update_player(&player, (PlayerInput){.forward = 1.0f}, 0.2f, &world);

  assert(!player.grounded);
  assert(player.position.z < -0.5f);
  assert(player.position.y < 0.0f);
  assert(player.velocityY < 0.0f);
}

static void test_jump_requires_grounding(void) {
  World world = single_block_world();
  Player grounded = make_player((Vec3){0.0f, 0.0f, 0.0f}, 1);
  Player airborne = make_player((Vec3){0.0f, 2.0f, 0.0f}, 0);

  update_player(&grounded, (PlayerInput){.jump = 1}, 0.1f, &world);
  update_player(&airborne, (PlayerInput){.jump = 1}, 0.1f, &world);

  assert(!grounded.grounded);
  assert(grounded.velocityY > 0.0f);
  assert(grounded.position.y > 0.0f);
  assert(!airborne.grounded);
  assert_near(airborne.velocityY, -9.81f * 0.1f);
}

int main(void) {
  test_vector_normalization();
  test_matrix_multiply_identity();
  test_diagonal_movement_is_normalized();
  test_landing_on_block();
  test_falling_off_platform_edge();
  test_jump_requires_grounding();
  puts("All game logic tests passed.");
  return 0;
}