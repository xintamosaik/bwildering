#include "game_logic.h"

#include <math.h>
#include <limits.h>
#include <stddef.h>

/* Keep roughly 1.25 blocks of clearance with a brisk rise and heavier descent. */
static const float GRAVITY = 20.0f;
static const float JUMP_SPEED = 7.2f;
static const float FALL_GRAVITY_MULTIPLIER = 1.5f;
/* Muted earth tones: olive, warm stone, dusty teal, and ochre. */
static const Vec3 BLOCK_TYPE_COLORS[BLOCK_TYPE_COUNT] = {
  [BLOCK_TYPE_GRASS] = {.x = 0.46f, .y = 0.52f, .z = 0.38f},
  [BLOCK_TYPE_STONE] = {.x = 0.53f, .y = 0.51f, .z = 0.48f},
  [BLOCK_TYPE_WATER] = {.x = 0.36f, .y = 0.50f, .z = 0.52f},
  [BLOCK_TYPE_DIRT] = {.x = 0.40f, .y = 0.28f, .z = 0.18f},
  [BLOCK_TYPE_GRAVEL] = {.x = 0.43f, .y = 0.46f, .z = 0.49f},
  [BLOCK_TYPE_DARK_STONE] = {.x = 0.24f, .y = 0.25f, .z = 0.28f},
  [BLOCK_TYPE_SNOW] = {.x = 0.92f, .y = 0.94f, .z = 0.96f},
  [BLOCK_TYPE_SAND] = {.x = 0.68f, .y = 0.60f, .z = 0.44f},
};

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
  const Vec3 front = player->aim.front;
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
  if (!block_is_material(type) || !player_view_block(world, player, reach, hit, adjacent))
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

/* Measure actual water overlapping the body, including partial immersion,
   edited pools, translated/scaled worlds and signed chunk boundaries. */
static float player_water_fraction(const World *const world, const Vec3 p) {
  if (!world || !world->chunks) return 0.0f;
  const float size = world->blockSize;
  const float lo[3] = {(p.x - PLAYER_RADIUS - world->origin.x) / size,
                       (p.y - world->origin.y) / size,
                       (p.z - PLAYER_RADIUS - world->origin.z) / size};
  const float hi[3] = {(p.x + PLAYER_RADIUS - world->origin.x) / size,
                       (p.y + PLAYER_HEIGHT - world->origin.y) / size,
                       (p.z + PLAYER_RADIUS - world->origin.z) / size};
  const int lower[3] = {world->minX, WORLD_COORD_MIN, world->minZ};
  const int upper[3] = {world->minX + world->blocksPerSide, WORLD_COORD_MAX + 1,
                        world->minZ + world->blocksPerSide};
  int first[3], last[3];
  for (int axis = 0; axis < 3; axis++) {
    if (!isfinite(lo[axis]) || !isfinite(hi[axis]) ||
        hi[axis] <= (float)lower[axis] || lo[axis] >= (float)upper[axis]) return 0.0f;
    first[axis] = (int)floorf(fmaxf(lo[axis], (float)lower[axis]));
    last[axis] = (int)ceilf(fminf(hi[axis], (float)upper[axis])) - 1;
  }
  const float bodyVolume = (hi[0] - lo[0]) * (hi[1] - lo[1]) * (hi[2] - lo[2]);
  if (bodyVolume <= 0.0f) return 0.0f;
  float waterVolume = 0.0f;
  for (int x = first[0]; x <= last[0]; x++)
    for (int y = first[1]; y <= last[1]; y++)
      for (int z = first[2]; z <= last[2]; z++) {
        if (world_get_block(world, x, y, z) != BLOCK_TYPE_WATER) continue;
        const float dx = fminf(hi[0], (float)x + 1.0f) - fmaxf(lo[0], (float)x);
        const float dy = fminf(hi[1], (float)y + 1.0f) - fmaxf(lo[1], (float)y);
        const float dz = fminf(hi[2], (float)z + 1.0f) - fmaxf(lo[2], (float)z);
        waterVolume += dx * dy * dz;
      }
  return fmaxf(0.0f, fminf(1.0f, waterVolume / bodyVolume));
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
      hiX <= (float)world->minX || loX >= (float)world->minX + (float)world->blocksPerSide ||
      hiZ <= (float)world->minZ || loZ >= (float)world->minZ + (float)world->blocksPerSide)
    return 0;
  const int firstX = (int)floorf(fmaxf(loX, (float)world->minX));
  const int lastX = (int)ceilf(fminf(hiX, (float)world->minX + (float)world->blocksPerSide)) - 1;
  const int firstZ = (int)floorf(fmaxf(loZ, (float)world->minZ));
  const int lastZ = (int)ceilf(fminf(hiZ, (float)world->minZ + (float)world->blocksPerSide)) - 1;

  /* Use the highest support beneath any part of the body footprint. */
  float height = -INFINITY;
  for (int x = firstX; x <= lastX; x++)
    for (int z = firstZ; z <= lastZ; z++) {
      const float top = world_support_height(world, world->origin.x + ((float)x + 0.5f) * world->blockSize,
                                       world->origin.z + ((float)z + 0.5f) * world->blockSize);
      if (isfinite(top)) height = fmaxf(height, top);
    }
  if (!isfinite(height)) return 0;

  player->position.y = height;
  player->velocityY = 0;
  player->grounded = 1;
  return 1;
}

static void player_step(Player *const updatedPlayer, const PlayerInput input, const float deltaTime, const World *const world) {
  /* Resolve horizontal motion one axis at a time to slide along walls. */
  const float immersion = player_water_fraction(world, updatedPlayer->position);
  updatedPlayer->crouching = input.crouch && immersion <= 0.001f;
  const float drySpeed = 4.0f * (updatedPlayer->crouching ? 0.75f : input.run ? 1.5f : 1.0f);
  const float swimSpeed = input.run ? 2.8f : 2.0f;
  const float wetWeight = fminf(1.0f, immersion * 2.0f);
  const float speed = drySpeed + (swimSpeed - drySpeed) * wetWeight;
  /* Fully immersed forward movement follows the view; at the surface it stays
     horizontal so looking around does not force the player underwater. */
  const Vec3 forward = immersion > 0.8f ? updatedPlayer->aim.front :
      vec3_normalize((Vec3){.x = updatedPlayer->aim.front.x, .y = 0.0f, .z = updatedPlayer->aim.front.z});
  const Vec3 right = vec3_normalize(vec3_cross(forward, (Vec3){0.0f, 1.0f, 0.0f}));
  Vec3 movement =
      vec3_add(vec3_scale(forward, input.forward), vec3_scale(right, input.strafe));
  if (vec3_dot(movement, movement) > 1)
    movement = vec3_normalize(movement);
  move_axis(updatedPlayer, world, movement.x * speed * deltaTime, 0);
  move_axis(updatedPlayer, world, movement.z * speed * deltaTime, 2);

  const float submerged = player_water_fraction(world, updatedPlayer->position);
  if (submerged > 0.001f) {
    /* Buoyancy balances at about 40% immersion, leaving the eyes above water.
       Exponential drag damps incoming falls and avoids locking a fixed height. */
    const float drag = 5.0f;
    const float neutralTarget = (15.0f * submerged - 6.0f) / drag;
    const int up = input.swimUp || input.jump;
    const float vertical = up || input.crouch ?
        (float)up - (float)(input.crouch != 0) : movement.y;
    const float target = fabsf(vertical) > 0.001f ?
        vertical * (vertical > 0.0f ? 3.0f : 2.5f) : neutralTarget;
    updatedPlayer->crouching = 0;
    updatedPlayer->grounded = 0;
    updatedPlayer->velocityY = target + (updatedPlayer->velocityY - target) * expf(-drag * deltaTime);
    updatedPlayer->velocityY = fmaxf(-4.0f, fminf(3.0f, updatedPlayer->velocityY));
    const int falling = updatedPlayer->velocityY < 0.0f;
    if (move_axis(updatedPlayer, world, updatedPlayer->velocityY * deltaTime, 1)) {
      updatedPlayer->velocityY = 0.0f;
      updatedPlayer->grounded = falling;
    }
    return;
  }

  /* Recheck support after moving, then accept a grounded jump. */
  Vec3 below = updatedPlayer->position;
  below.y -= 0.001f;
  if (updatedPlayer->grounded && !player_intersects(world, below))
    updatedPlayer->grounded = 0;
  if (input.jump && updatedPlayer->grounded) {
    updatedPlayer->velocityY = JUMP_SPEED;
    updatedPlayer->grounded = 0;
  }
  if (updatedPlayer->grounded)
    return;

  const float gravity = GRAVITY * (updatedPlayer->velocityY <= 0 ? FALL_GRAVITY_MULTIPLIER : 1);
  updatedPlayer->velocityY -= gravity * deltaTime;
  const int falling = updatedPlayer->velocityY < 0;
  if (move_axis(updatedPlayer, world, updatedPlayer->velocityY * deltaTime, 1)) {
    updatedPlayer->velocityY = 0;
    updatedPlayer->grounded = falling;
  }
}

void update_player(Player *const player, const PlayerInput input, const float deltaTime, const World *const world) {
  if (!player || !isfinite(deltaTime) || deltaTime <= 0.0f || deltaTime > 10.0f) return;
  /* Keep the established land timestep. Wet motion uses bounded steps so a
     long update cannot propel the player far above the surface in one step. */
  const int steps = player_water_fraction(world, player->position) > 0.001f ?
      (int)ceilf(deltaTime * 60.0f) : 1;
  const float step = deltaTime / (float)steps;
  for (int i = 0; i < steps; i++) {
    PlayerInput sampled = input;
    if (i) sampled.jump = 0;
    player_step(player, sampled, step, world);
  }
}
