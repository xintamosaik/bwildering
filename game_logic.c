#include "game_logic.h"

#include <math.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>

/* Keep roughly 1.25 blocks of clearance with a brisk rise and heavier descent. */
static const float GRAVITY = 20.0f;
static const float JUMP_SPEED = 7.2f;
static const float FALL_GRAVITY_MULTIPLIER = 1.5f;
/* Muted earth tones: olive, warm stone, dusty teal, and ochre. */
static const Vec3 BLOCK_TYPE_COLORS[BLOCK_TYPE_COUNT] = {
  [BLOCK_TYPE_GRASS] = {.x = 0.46f, .y = 0.52f, .z = 0.38f},
  [BLOCK_TYPE_STONE] = {.x = 0.53f, .y = 0.51f, .z = 0.48f},
  [BLOCK_TYPE_WATER] = {.x = 0.36f, .y = 0.50f, .z = 0.52f},
  [BLOCK_TYPE_SAND] = {.x = 0.68f, .y = 0.60f, .z = 0.44f},
};

static uint32_t next_random(uint32_t *state) {
  uint32_t value = *state;
  value ^= value << 13;
  value ^= value >> 17;
  value ^= value << 5;
  *state = value;
  return value;
}

uint32_t random_bounded(uint32_t *state, uint32_t upperBound) {
  if (state == NULL || upperBound == 0u)
    return 0u;

  const uint32_t threshold = (0u - upperBound) % upperBound;
  uint32_t value;
  do {
    value = next_random(state);
  } while (value < threshold);

  return value % upperBound;
}

Vec3 vec3_add(const Vec3 a, const Vec3 b) {
  return (Vec3){.x = a.x + b.x, .y = a.y + b.y, .z = a.z + b.z};
}

Vec3 vec3_sub(const Vec3 a, const Vec3 b) {
  return (Vec3){.x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z};
}

Vec3 vec3_scale(const Vec3 v, const float scalar) {
  return (Vec3){.x = v.x * scalar, .y = v.y * scalar, .z = v.z * scalar};
}

Vec3 vec3_normalize(const Vec3 v) {
  const float length = sqrtf(vec3_dot(v, v));
  if (length == 0.0f)
    return (Vec3){.x = 0.0f, .y = 0.0f, .z = 0.0f};
  return vec3_scale(v, 1.0f / length);
}

Vec3 vec3_cross(const Vec3 a, const Vec3 b) {
  return (Vec3){.x = a.y * b.z - a.z * b.y, .y = a.z * b.x - a.x * b.z,
                .z = a.x * b.y - a.y * b.x};
}

float vec3_dot(const Vec3 a, const Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

int block_type_color(const BlockType type, Vec3 *color) {
  if (color == NULL || type < BLOCK_TYPE_GRASS || type >= BLOCK_TYPE_COUNT)
    return 0;

  *color = BLOCK_TYPE_COLORS[type];
  return 1;
}

void mat4_rotate(mat4 dest, const float angle, float x, float y, float z) {
  const float c = cosf(angle);
  const float s = sinf(angle);
  const float len = sqrtf(x * x + y * y + z * z);
  if (len == 0.0f) {
    mat4_identity(dest);
    return;
  }
  x /= len;
  y /= len;
  z /= len;
  const float ic = 1.0f - c;

  mat4_identity(dest);
  dest[0] = x * x * ic + c;
  dest[4] = x * y * ic - z * s;
  dest[8] = x * z * ic + y * s;
  dest[1] = y * x * ic + z * s;
  dest[5] = y * y * ic + c;
  dest[9] = y * z * ic - x * s;
  dest[2] = x * z * ic - y * s;
  dest[6] = y * z * ic + x * s;
  dest[10] = z * z * ic + c;
}

void mat4_identity(mat4 dest) {
  for (int i = 0; i < 16; i++)
    dest[i] = 0.0f;
  dest[0] = dest[5] = dest[10] = dest[15] = 1.0f;
}

void mat4_perspective(const float fov_radians, const float aspect, const float near, const float far,
                      mat4 dest) {
  for (int i = 0; i < 16; i++)
    dest[i] = 0.0f;
  const float tanHalfFovy = tanf(fov_radians / 2.0f);

  dest[0] = 1.0f / (aspect * tanHalfFovy);
  dest[5] = 1.0f / tanHalfFovy;
  dest[10] = -(far + near) / (far - near);
  dest[11] = -1.0f;
  dest[14] = -(2.0f * far * near) / (far - near);
}

void mat4_lookat(const Vec3 eye, const Vec3 target, const Vec3 up, mat4 dest) {
  const Vec3 f = vec3_normalize(vec3_sub(target, eye));
  const Vec3 s = vec3_normalize(vec3_cross(f, up));
  const Vec3 u = vec3_cross(s, f);

  mat4_identity(dest);
  dest[0] = s.x;
  dest[4] = s.y;
  dest[8] = s.z;
  dest[1] = u.x;
  dest[5] = u.y;
  dest[9] = u.z;
  dest[2] = -f.x;
  dest[6] = -f.y;
  dest[10] = -f.z;

  dest[12] = -vec3_dot(s, eye);
  dest[13] = -vec3_dot(u, eye);
  dest[14] = vec3_dot(f, eye);
}

void mat4_multiply(const mat4 a, const mat4 b, mat4 dest) {
  mat4 res;
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      res[j * 4 + i] =
          a[0 * 4 + i] * b[j * 4 + 0] + a[1 * 4 + i] * b[j * 4 + 1] +
          a[2 * 4 + i] * b[j * 4 + 2] + a[3 * 4 + i] * b[j * 4 + 3];
    }
  }
  for (int i = 0; i < 16; i++)
    dest[i] = res[i];
}

/* Integer hashing makes the seed independent of global RNG state. */
static float terrain_lattice(const int x, const int z, const uint32_t seed) {
  uint32_t hash = seed ^ ((uint32_t)x * UINT32_C(0x9E3779B9)) ^
                         ((uint32_t)z * UINT32_C(0x85EBCA6B));
  hash ^= hash >> 16;
  hash *= UINT32_C(0x7FEB352D);
  hash ^= hash >> 15;
  hash *= UINT32_C(0x846CA68B);
  hash ^= hash >> 16;
  return (float)(hash & UINT32_C(0xFFFF)) / 65535.0f;
}

float terrain_noise(const int x, const int z, const int scale,
                           const uint32_t seed) {
  if (scale != 8 && scale != 16 && scale != 32)
    return NAN;
  const int gx = voxel_floor_div(x, scale);
  const int gz = voxel_floor_div(z, scale);
  float tx = (float)voxel_floor_mod(x, scale) / (float)scale;
  float tz = (float)voxel_floor_mod(z, scale) / (float)scale;
  tx = tx * tx * (3.0f - 2.0f * tx);
  tz = tz * tz * (3.0f - 2.0f * tz);
  const float a = terrain_lattice(gx, gz, seed);
  const float b = terrain_lattice(gx + 1, gz, seed);
  const float c = terrain_lattice(gx, gz + 1, seed);
  const float d = terrain_lattice(gx + 1, gz + 1, seed);
  return (a + (b - a) * tx) * (1.0f - tz) + (c + (d - c) * tx) * tz;
}

TerrainColumn terrain_sample_column(uint32_t seed, int x, int z) {
  int height = (int)(10.0f * terrain_noise(x, z, 32, seed) +
                     4.0f * terrain_noise(x, z, 16, seed ^ UINT32_C(0xA341316C)) +
                     1.5f * terrain_noise(x, z, 8, seed ^ UINT32_C(0xC8013EA4)));
  BlockType type = height <= 4    ? BLOCK_TYPE_SAND
                   : height >= 10 ? BLOCK_TYPE_STONE
                                  : BLOCK_TYPE_GRASS;
  return (TerrainColumn){height, type};
}

/* The flat platform retains its global shuffle fixture. */
int create_platform(World *world, int side, Vec3 center, float size, uint32_t seed) {
  World result = {0};
  if (world == NULL || !world_init(&result, side, center, size))
    return 0;
  const int count = side * side;
  uint8_t *tiles = malloc((size_t)count);
  if (tiles == NULL) {
    world_destroy(&result);
    return 0;
  }
  for (int i = 0; i < count; i++)
    tiles[i] = (uint8_t)(i % BLOCK_TYPE_COUNT);
  uint32_t state = seed == 0 ? UINT32_C(0x6D2B79F5) : seed;
  for (int i = count - 1; i > 0; i--) {
    int j = (int)random_bounded(&state, (uint32_t)(i + 1));
    uint8_t t = tiles[i];
    tiles[i] = tiles[j];
    tiles[j] = t;
  }
  for (int x = 0; x < side; x++) {
    for (int z = 0; z < side; z++) {
      int height = 0;
      BlockType type = (BlockType)tiles[x * side + z];
      for (int y = -1; y < height; y++) {
        if (!world_set_block(&result, result.minX + x, y, result.minZ + z, type)) {
          free(tiles);
          world_destroy(&result);
          return 0;
        }
      }
    }
  }
  free(tiles);
  world_destroy(world);
  *world = result;
  return 1;
}

int create_terrain_region(World *world, int minX, int minZ, int side,
                          Vec3 origin, float size, uint32_t seed) {
  World result = {0};
  if (!world || !world_init_region(&result, minX, minZ, side, origin, size))
    return 0;
  for (int x = minX; x < minX + side; x++)
    for (int z = minZ; z < minZ + side; z++) {
      TerrainColumn column = terrain_sample_column(seed, x, z);
      for (int y = -1; y < column.height; y++)
        if (!world_set_block(&result, x, y, z, column.material)) {
          world_destroy(&result);
          return 0;
        }
    }
  world_destroy(world);
  *world = result;
  return 1;
}

int create_terrain(World *world, int side, Vec3 center, float size, uint32_t seed) {
  Vec3 origin = center;
  origin.x -= (float)(side % 2) * size * 0.5f;
  origin.z -= (float)(side % 2) * size * 0.5f;
  return create_terrain_region(world, -(side / 2), -(side / 2), side, origin, size, seed);
}

/* The body is independent of camera eye height. */
static const float PLAYER_RADIUS = 0.25f;
static const float PLAYER_HEIGHT = 1.5f;

static int player_view_block(const World *world, const Player *player, float reach,
                             int hit[3], int adjacent[3]) {
  if (!world || !world->chunks || !player ||
      !isfinite(reach) || reach <= 0)
    return 0;
  Vec3 eye = player_eye_position(player);
  Vec3 front = player->camera.front;
  double length = sqrt((double)front.x * front.x + (double)front.y * front.y +
                       (double)front.z * front.z);
  if (!isfinite(length) || length == 0)
    return 0;
  double origin[3] = {(double)eye.x - world->origin.x,
                      (double)eye.y - world->origin.y,
                      (double)eye.z - world->origin.z};
  double direction[3] = {front.x / length, front.y / length, front.z / length};
  double next[3], interval[3];
  int cell[3], previous[3], step[3];
  for (int axis = 0; axis < 3; axis++) {
    origin[axis] /= world->blockSize;
    double coordinate = floor(origin[axis]);
    if (!isfinite(coordinate) || coordinate <= INT_MIN || coordinate >= INT_MAX)
      return 0;
    previous[axis] = cell[axis] = (int)coordinate;
    step[axis] = direction[axis] < 0 ? -1 : 1;
    interval[axis] = direction[axis] == 0 ? INFINITY :
        world->blockSize / fabs(direction[axis]);
    next[axis] = direction[axis] == 0 ? INFINITY :
        (direction[axis] < 0 ? origin[axis] - cell[axis]
                             : cell[axis] + 1.0 - origin[axis]) * interval[axis];
  }
  if (block_is_solid(world_get_block(world, cell[0], cell[1], cell[2])))
    return 0;
  /* Traverse voxel faces exactly, so small blocks and section seams cannot be skipped. */
  for (;;) {
    int axis = next[0] <= next[1] ? 0 : 1;
    if (next[2] < next[axis])
      axis = 2;
    if (next[axis] > reach ||
        (step[axis] < 0 ? cell[axis] == INT_MIN : cell[axis] == INT_MAX))
      return 0;
    for (int i = 0; i < 3; i++)
      previous[i] = cell[i];
    cell[axis] += step[axis];
    next[axis] += interval[axis];
    if (!block_is_solid(world_get_block(world, cell[0], cell[1], cell[2])))
      continue;
    for (int i = 0; i < 3; i++) {
      hit[i] = cell[i];
      adjacent[i] = previous[i];
    }
    return 1;
  }
}

int player_place_block(World *world, const Player *player, BlockType type, float reach) {
  int hit[3], adjacent[3];
  if (!block_is_solid(type) || !player_view_block(world, player, reach, hit, adjacent))
    return 0;
  double low[3] = {world->origin.x + (double)adjacent[0] * world->blockSize,
                   world->origin.y + (double)adjacent[1] * world->blockSize,
                   world->origin.z + (double)adjacent[2] * world->blockSize};
  if (low[0] < player->position.x + PLAYER_RADIUS &&
      low[0] + world->blockSize > player->position.x - PLAYER_RADIUS &&
      low[1] < player->position.y + PLAYER_HEIGHT &&
      low[1] + world->blockSize > player->position.y &&
      low[2] < player->position.z + PLAYER_RADIUS &&
      low[2] + world->blockSize > player->position.z - PLAYER_RADIUS)
    return 0;
  return world_set_block(world, adjacent[0], adjacent[1], adjacent[2], type);
}

int player_remove_block(World *world, const Player *player, float reach) {
  int hit[3], adjacent[3];
  if (!player_view_block(world, player, reach, hit, adjacent))
    return 0;
  return world_set_block(world, hit[0], hit[1], hit[2], BLOCK_TYPE_EMPTY);
}

static int player_intersects(const World *world, Vec3 p) {
  if (world == NULL || world->chunks == NULL)
    return 0;
  const float size = world->blockSize;
  const float epsilon = 0.00001f;
  float lo[3] = {(p.x - PLAYER_RADIUS - world->origin.x) / size,
                 (p.y + epsilon - world->origin.y) / size,
                 (p.z - PLAYER_RADIUS - world->origin.z) / size};
  float hi[3] = {(p.x + PLAYER_RADIUS - world->origin.x) / size,
                 (p.y + PLAYER_HEIGHT - epsilon - world->origin.y) / size,
                 (p.z + PLAYER_RADIUS - world->origin.z) / size};
  int lower[3] = {world->minX, WORLD_MIN_SECTION_Y * SECTION_SIZE, world->minZ};
  int upper[3] = {world->minX + world->blocksPerSide,
                  (WORLD_MIN_SECTION_Y + WORLD_SECTION_COUNT) * SECTION_SIZE,
                  world->minZ + world->blocksPerSide};
  int first[3], last[3];
  for (int i = 0; i < 3; i++) {
    if (!isfinite(lo[i]) || !isfinite(hi[i]) || hi[i] <= (float)lower[i] || lo[i] >= (float)upper[i])
      return 0;
    first[i] = (int)floorf(fmaxf(lo[i], (float)lower[i]));
    last[i] = (int)ceilf(fminf(hi[i], (float)upper[i])) - 1;
  }
  for (int x = first[0]; x <= last[0]; x++)
    for (int y = first[1]; y <= last[1]; y++)
      for (int z = first[2]; z <= last[2]; z++)
        if (block_is_solid(world_get_block(world, x, y, z)))
          return 1;
  return 0;
}

static int move_axis(Player *player, const World *world, float distance, int axis) {
  float size = world && world->chunks ? world->blockSize : 1.0f;
  int steps = (int)fmaxf(1, ceilf(fabsf(distance) / fminf(size * 0.25f, 0.125f)));
  for (int i = 0; i < steps; i++) {
    const Vec3 start = player->position;
    Vec3 next = start;
    const float step = distance / (float)steps;
    if (axis == 0)
      next.x += step;
    else if (axis == 1)
      next.y += step;
    else
      next.z += step;
    if (player_intersects(world, next)) {
      float low = 0, high = 1;
      for (int j = 0; j < 22; j++) {
        float t = (low + high) * 0.5f;
        Vec3 candidate = vec3_add(start, vec3_scale(vec3_sub(next, start), t));
        if (player_intersects(world, candidate))
          high = t;
        else
          low = t;
      }
      /* Leave a small separation so float rounding cannot place the body
         just beyond the contact plane on the next frame. */
      low = fmaxf(0, low - 0.000001f / fmaxf(fabsf(step), 0.000001f));
      player->position = vec3_add(start, vec3_scale(vec3_sub(next, start), low));
      return 1;
    }
    player->position = next;
  }
  return 0;
}

Vec3 player_eye_position(const Player *player) {
  Vec3 eye = player->position;
  eye.y += player->eyeHeight - (player->crouching ? 0.3f : 0.0f);
  return eye;
}

void update_player(Player *player, PlayerInput input, float dt, const World *world) {
  if (!player || !isfinite(dt) || dt <= 0 || dt > 10)
    return;
  player->crouching = input.crouch != 0;
  float speed = 4.0f * (input.crouch ? 0.75f : input.run ? 1.5f : 1.0f);
  Vec3 forward =
      vec3_normalize((Vec3){player->camera.front.x, 0, player->camera.front.z});
  Vec3 right = vec3_normalize(vec3_cross(forward, player->camera.up));
  Vec3 movement =
      vec3_add(vec3_scale(forward, input.forward), vec3_scale(right, input.strafe));
  if (vec3_dot(movement, movement) > 1)
    movement = vec3_normalize(movement);
  move_axis(player, world, movement.x * speed * dt, 0);
  move_axis(player, world, movement.z * speed * dt, 2);
  Vec3 below = player->position;
  below.y -= 0.001f;
  if (player->grounded && !player_intersects(world, below))
    player->grounded = 0;
  if (input.jump && player->grounded) {
    player->velocityY = JUMP_SPEED;
    player->grounded = 0;
  }
  if (player->grounded)
    return;
  float gravity = GRAVITY * (player->velocityY <= 0 ? FALL_GRAVITY_MULTIPLIER : 1);
  player->velocityY -= gravity * dt;
  int falling = player->velocityY < 0;
  if (move_axis(player, world, player->velocityY * dt, 1)) {
    player->velocityY = 0;
    player->grounded = falling;
  }
}
