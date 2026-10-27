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
  [BLOCK_TYPE_DIRT] = {0.40f, 0.28f, 0.18f},
  [BLOCK_TYPE_GRAVEL] = {0.43f, 0.46f, 0.49f},
  [BLOCK_TYPE_DARK_STONE] = {0.24f, 0.25f, 0.28f},
  [BLOCK_TYPE_SNOW] = {0.92f, 0.94f, 0.96f},
  [BLOCK_TYPE_SAND] = {.x = 0.68f, .y = 0.60f, .z = 0.44f},
};

static uint32_t next_random(uint32_t *const state) {
  uint32_t value = *state;
  value ^= value << 13;
  value ^= value >> 17;
  value ^= value << 5;
  *state = value;
  return value;
}

uint32_t random_bounded(uint32_t *const state, const uint32_t upperBound) {
  if (state == NULL || upperBound == 0u)
    return 0u;

  if (*state == 0u)
    *state = UINT32_C(0x6D2B79F5);

  /* Reject a partial modulo cycle to keep bounded results unbiased. */
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

int block_type_color(const BlockType type, Vec3 *const color) {
  if (color == NULL || type < BLOCK_TYPE_GRASS || type >= BLOCK_TYPE_COUNT)
    return 0;

  *color = BLOCK_TYPE_COLORS[type];
  return 1;
}

void mat4_rotate(float dest[const 16], const float angle, float x, float y, float z) {
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

void mat4_identity(float dest[const 16]) {
  for (int i = 0; i < 16; i++)
    dest[i] = 0.0f;
  dest[0] = dest[5] = dest[10] = dest[15] = 1.0f;
}

void mat4_perspective(const float fov_radians, const float aspect, const float near, const float far,
                      float dest[const 16]) {
  for (int i = 0; i < 16; i++)
    dest[i] = 0.0f;
  const float tanHalfFovy = tanf(fov_radians / 2.0f);

  dest[0] = 1.0f / (aspect * tanHalfFovy);
  dest[5] = 1.0f / tanHalfFovy;
  dest[10] = -(far + near) / (far - near);
  dest[11] = -1.0f;
  dest[14] = -(2.0f * far * near) / (far - near);
}

void mat4_lookat(const Vec3 eye, const Vec3 target, const Vec3 up, float dest[const 16]) {
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

void mat4_multiply(const float a[const 16], const float b[const 16], float dest[const 16]) {
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
  if (scale < 8 || scale > 2048 || (scale & (scale - 1)) != 0)
    return NAN;

  const int gx = voxel_floor_div(x, scale);
  const int gz = voxel_floor_div(z, scale);
  float tx = (float)voxel_floor_mod(x, scale) / (float)scale;
  float tz = (float)voxel_floor_mod(z, scale) / (float)scale;
  tx = tx * tx * (3.0f - 2.0f * tx);
  tz = tz * tz * (3.0f - 2.0f * tz);

  /* Interpolate the four lattice corners with smoothed local coordinates. */
  const float a = terrain_lattice(gx, gz, seed);
  const float b = terrain_lattice(gx + 1, gz, seed);
  const float c = terrain_lattice(gx, gz + 1, seed);
  const float d = terrain_lattice(gx + 1, gz + 1, seed);
  return (a + (b - a) * tx) * (1.0f - tz) + (c + (d - c) * tx) * tz;
}

static int layer_thickness(const float n, const int lo, const int hi) {
  const int v = lo + (int)(n * (hi - lo + 1));
  return v > hi ? hi : v;
}

TerrainColumn terrain_sample_column(const uint32_t seed, const int x, const int z) {
  /* Compact relief: the main shapes are four times narrower than the initial
     tuning, giving the same height changes much steeper sides. */
  const float broad = terrain_noise(x, z, TERRAIN_RELIEF_SCALE, seed);
  const float region = terrain_noise(x, z, TERRAIN_MOUNTAIN_REGION_SCALE, seed ^ UINT32_C(0xA341316C));
  float mask = fmaxf(0, fminf(1, (region - 0.45f) / 0.35f));
  mask = mask * mask * (3 - 2 * mask);
  const float ridge = 1 - fabsf(2 * terrain_noise(x, z, TERRAIN_RIDGE_SCALE, seed ^ UINT32_C(0xC8013EA4)) - 1);
  const float hills = terrain_noise(x, z, TERRAIN_HILL_SCALE, seed ^ UINT32_C(0x7F4A7C15));
  const float detail = terrain_noise(x, z, TERRAIN_DETAIL_SCALE, seed ^ UINT32_C(0x912AC318));
  int height = (int)floorf(TERRAIN_SURFACE_MIN + 175 * broad + 285 * mask * ridge + 18 * hills + 4 * detail);
  if (height > TERRAIN_SURFACE_MAX) height = TERRAIN_SURFACE_MAX;

  /* Choose surface layers after fixing the column height. */
  const float local = terrain_noise(x, z, 64, seed ^ UINT32_C(0xFE321AB7));
  const float deposit = terrain_noise(x, z, 128, seed ^ UINT32_C(0xBC219841));
  const int offset = (int)floorf(12 * local - 6);
  TerrainColumn c = {.height = height, .material = BLOCK_TYPE_STONE,
                    .deepRockY = TERRAIN_DEEP_ROCK_Y + offset};
  if (height - 1 >= TERRAIN_SNOW_Y + offset) {
    c.snowDepth = layer_thickness(local, TERRAIN_SNOW_MIN, TERRAIN_SNOW_MAX);
    c.material = BLOCK_TYPE_SNOW;
  } else if (height - 1 < TERRAIN_STONE_Y + offset) {
    if (height - 1 >= TERRAIN_SHORE_MIN + offset / 3 &&
        height - 1 <= TERRAIN_SHORE_MAX + offset / 3 && deposit > 0.35f) {
      c.gravelDepth = layer_thickness(local, TERRAIN_GRAVEL_MIN, TERRAIN_GRAVEL_MAX);
      if (deposit > 0.55f) c.sandDepth = layer_thickness(local, TERRAIN_SAND_MIN, TERRAIN_SAND_MAX);
      c.material = c.sandDepth ? BLOCK_TYPE_SAND : BLOCK_TYPE_GRAVEL;
    } else if (height - 1 > TERRAIN_SEA_LEVEL + 5) {
      c.soilDepth = layer_thickness(local, TERRAIN_SOIL_MIN, TERRAIN_SOIL_MAX);
      c.material = BLOCK_TYPE_GRASS;
    }
  }

  if (height - 1 < c.deepRockY) c.material = BLOCK_TYPE_DARK_STONE;
  return c;
}

BlockType terrain_column_block(const TerrainColumn c, const int y) {
  if (y < TERRAIN_BASE_Y || y >= c.height) return BLOCK_TYPE_EMPTY;
  if (y < c.deepRockY) return BLOCK_TYPE_DARK_STONE;
  const int depth = c.height - 1 - y;
  if (depth < c.snowDepth) return BLOCK_TYPE_SNOW;
  if (depth < c.sandDepth) return BLOCK_TYPE_SAND;
  if (depth < c.sandDepth + c.gravelDepth) return BLOCK_TYPE_GRAVEL;
  if (c.soilDepth && depth == 0) return BLOCK_TYPE_GRASS;
  if (c.soilDepth && depth <= c.soilDepth) return BLOCK_TYPE_DIRT;
  return BLOCK_TYPE_STONE;
}

/* The flat platform retains its global shuffle fixture. */
int create_platform(World *const world, const int side, const Vec3 center, const float size, const uint32_t seed) {
  World result = {0};
  if (world == NULL || !world_init(&result, side, center, size))
    return 0;
  const int count = side * side;
  uint8_t *const tiles = malloc((size_t)count);
  if (tiles == NULL) {
    world_destroy(&result);
    return 0;
  }
  for (int i = 0; i < count; i++)
    tiles[i] = (uint8_t)(i % BLOCK_TYPE_COUNT);
  uint32_t state = seed == 0 ? UINT32_C(0x6D2B79F5) : seed;
  for (int i = count - 1; i > 0; i--) {
    const int j = (int)random_bounded(&state, (uint32_t)(i + 1));
    const uint8_t t = tiles[i];
    tiles[i] = tiles[j];
    tiles[j] = t;
  }

  /* Apply the shuffled palette to the platform cells. */
  for (int x = 0; x < side; x++) {
    for (int z = 0; z < side; z++) {
      const int height = 0;
      const BlockType type = (BlockType)tiles[x * side + z];
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

int create_terrain_region(World *const world, const int minX, const int minZ, const int side,
                          const Vec3 origin, const float size, const uint32_t seed) {
  World result = {0};
  if (!world || !world_init_region(&result, minX, minZ, side, origin, size))
    return 0;
  result.proceduralTerrain = 1;
  result.terrainSeed = seed;
  for (int x = 0; x < result.chunksPerSide; x++)
    for (int z = 0; z < result.chunksPerSide; z++)
      if (!world_materialize_chunk(&result, result.minChunkX + x, result.minChunkZ + z)) {
        world_destroy(&result);
        return 0;
      }

  /* Publish the replacement only after every chunk is ready. */
  result.proceduralTerrain = 0;
  world_destroy(world);
  *world = result;
  return 1;
}

int create_terrain(World *const world, const int side, const Vec3 center, const float size, const uint32_t seed) {
  Vec3 origin = center;
  origin.x -= (float)(side % 2) * size * 0.5f;
  origin.z -= (float)(side % 2) * size * 0.5f;
  return create_terrain_region(world, -(side / 2), -(side / 2), side, origin, size, seed);
}

int create_terrain_streamed(World *const world, const int side, const Vec3 center, const float size, const uint32_t seed) {
  World result = {0};
  if (!world || !world_init(&result, side, center, size))
    return 0;
  result.proceduralTerrain = 1;
  result.terrainSeed = seed;
  world_destroy(world);
  *world = result;
  return 1;
}

/* The body is independent of camera eye height. */
static const float PLAYER_RADIUS = 0.25f;
static const float PLAYER_HEIGHT = 1.5f;

static int player_view_block(const World *const world, const Player *const player, const float reach,
                             int hit[const 3], int adjacent[const 3]) {
  if (!world || !world->chunks || !player ||
      !isfinite(reach) || reach <= 0)
    return 0;

  /* Normalize the ray so face-crossing distances use world units. */
  const Vec3 eye = player_eye_position(player);
  const Vec3 front = player->camera.front;
  const double length = sqrt((double)front.x * front.x + (double)front.y * front.y +
                       (double)front.z * front.z);
  if (!isfinite(length) || length == 0)
    return 0;

  double origin[3] = {(double)eye.x - world->origin.x,
                      (double)eye.y - world->origin.y,
                      (double)eye.z - world->origin.z};
  const double direction[3] = {front.x / length, front.y / length, front.z / length};
  double next[3], interval[3];
  int cell[3], previous[3], step[3];
  for (int axis = 0; axis < 3; axis++) {
    origin[axis] /= world->blockSize;
    const double coordinate = floor(origin[axis]);
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

int player_place_block(World *const world, const Player *const player, const BlockType type, const float reach) {
  int hit[3], adjacent[3];
  if (!block_is_solid(type) || !player_view_block(world, player, reach, hit, adjacent))
    return 0;

  /* Reject placement if the new block would overlap the player body. */
  const double low[3] = {world->origin.x + (double)adjacent[0] * world->blockSize,
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

int player_remove_block(World *const world, const Player *const player, const float reach) {
  int hit[3], adjacent[3];
  if (!player_view_block(world, player, reach, hit, adjacent))
    return 0;
  return world_set_block(world, hit[0], hit[1], hit[2], BLOCK_TYPE_EMPTY);
}

static int player_intersects(const World *const world, const Vec3 p) {
  if (world == NULL || world->chunks == NULL)
    return 0;
  const float size = world->blockSize;
  const float epsilon = 0.00001f;
  const float lo[3] = {(p.x - PLAYER_RADIUS - world->origin.x) / size,
                       (p.y + epsilon - world->origin.y) / size,
                       (p.z - PLAYER_RADIUS - world->origin.z) / size};
  const float hi[3] = {(p.x + PLAYER_RADIUS - world->origin.x) / size,
                       (p.y + PLAYER_HEIGHT - epsilon - world->origin.y) / size,
                       (p.z + PLAYER_RADIUS - world->origin.z) / size};
  const int lower[3] = {world->minX, WORLD_COORD_MIN, world->minZ};
  const int upper[3] = {world->minX + world->blocksPerSide,
                        WORLD_COORD_MAX + 1,
                        world->minZ + world->blocksPerSide};

  /* Clip the body bounds to the world before visiting overlapping cells. */
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

static int move_axis(Player *const player, const World *const world, const float distance, const int axis) {
  const float size = world && world->chunks ? world->blockSize : 1.0f;
  const int steps = (int)fmaxf(1, ceilf(fabsf(distance) / fminf(size * 0.25f, 0.125f)));
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
      /* Refine the blocked step to the last collision-free position. */
      float low = 0, high = 1;
      for (int j = 0; j < 22; j++) {
        const float t = (low + high) * 0.5f;
        const Vec3 candidate = vec3_add(start, vec3_scale(vec3_sub(next, start), t));
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

Vec3 player_eye_position(const Player *const player) {
  Vec3 eye = player->position;
  eye.y += player->eyeHeight - (player->crouching ? 0.3f : 0.0f);
  return eye;
}

int player_spawn_on_surface(Player *const player, const World *const world) {
  if (!player || !world || !world->chunks)
    return 0;
  const float insetRadius = PLAYER_RADIUS - 0.00001f;
  const float loX = (player->position.x - insetRadius - world->origin.x) / world->blockSize;
  const float hiX = (player->position.x + insetRadius - world->origin.x) / world->blockSize;
  const float loZ = (player->position.z - insetRadius - world->origin.z) / world->blockSize;
  const float hiZ = (player->position.z + insetRadius - world->origin.z) / world->blockSize;
  if (!isfinite(loX) || !isfinite(hiX) || !isfinite(loZ) || !isfinite(hiZ) ||
      hiX <= world->minX || loX >= world->minX + world->blocksPerSide ||
      hiZ <= world->minZ || loZ >= world->minZ + world->blocksPerSide)
    return 0;
  const int firstX = (int)floorf(fmaxf(loX, world->minX));
  const int lastX = (int)ceilf(fminf(hiX, world->minX + world->blocksPerSide)) - 1;
  const int firstZ = (int)floorf(fmaxf(loZ, world->minZ));
  const int lastZ = (int)ceilf(fminf(hiZ, world->minZ + world->blocksPerSide)) - 1;

  /* Use the highest support beneath any part of the body footprint. */
  float height = -INFINITY;
  for (int x = firstX; x <= lastX; x++)
    for (int z = firstZ; z <= lastZ; z++) {
      const float top = world_surface_height(world, world->origin.x + (x + 0.5f) * world->blockSize,
                                       world->origin.z + (z + 0.5f) * world->blockSize);
      if (isfinite(top)) height = fmaxf(height, top);
    }
  if (!isfinite(height)) return 0;

  player->position.y = height;
  player->velocityY = 0;
  player->grounded = 1;
  return 1;
}

void update_player(Player *const player, const PlayerInput input, const float dt, const World *const world) {
  if (!player || !isfinite(dt) || dt <= 0 || dt > 10)
    return;

  /* Resolve horizontal motion one axis at a time to slide along walls. */
  player->crouching = input.crouch != 0;
  const float speed = 4.0f * (input.crouch ? 0.75f : input.run ? 1.5f : 1.0f);
  const Vec3 forward =
      vec3_normalize((Vec3){player->camera.front.x, 0, player->camera.front.z});
  const Vec3 right = vec3_normalize(vec3_cross(forward, player->camera.up));
  Vec3 movement =
      vec3_add(vec3_scale(forward, input.forward), vec3_scale(right, input.strafe));
  if (vec3_dot(movement, movement) > 1)
    movement = vec3_normalize(movement);
  move_axis(player, world, movement.x * speed * dt, 0);
  move_axis(player, world, movement.z * speed * dt, 2);

  /* Recheck support after moving, then accept a grounded jump. */
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

  const float gravity = GRAVITY * (player->velocityY <= 0 ? FALL_GRAVITY_MULTIPLIER : 1);
  player->velocityY -= gravity * dt;
  const int falling = player->velocityY < 0;
  if (move_axis(player, world, player->velocityY * dt, 1)) {
    player->velocityY = 0;
    player->grounded = falling;
  }
}
