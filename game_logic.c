#include "game_logic.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>

static const float GRAVITY = 9.81f;
static const Vec3 BLOCK_TYPE_COLORS[BLOCK_TYPE_COUNT] = {
  [BLOCK_TYPE_GRASS] = {.x = 0.38f, .y = 0.62f, .z = 0.42f},
  [BLOCK_TYPE_STONE] = {.x = 0.55f, .y = 0.58f, .z = 0.60f},
  [BLOCK_TYPE_WATER] = {.x = 0.20f, .y = 0.48f, .z = 0.78f},
  [BLOCK_TYPE_SAND] = {.x = 0.86f, .y = 0.72f, .z = 0.42f},
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

int world_add_block(World *world, Block block) {
  if (world == NULL || world->blockCount < 0 ||
      world->blockCount >= WORLD_MAX_BLOCKS ||
      block.type < BLOCK_TYPE_GRASS || block.type >= BLOCK_TYPE_COUNT)
    return 0;

  world->blocks[world->blockCount++] = block;
  return 1;
}

int create_platform(World *world, const int blocksPerSide, const Vec3 center,
                    const float blockSize, const uint32_t seed) {
  if (world == NULL || blocksPerSide <= 0 ||
      blocksPerSide > WORLD_MAX_BLOCKS / blocksPerSide)
    return 0;

  const int totalBlocks = blocksPerSide * blocksPerSide;
  BlockType blockTypes[WORLD_MAX_BLOCKS];
  for (int i = 0; i < totalBlocks; i++)
    blockTypes[i] = (BlockType)(i % BLOCK_TYPE_COUNT);

  uint32_t randomState = seed == 0 ? UINT32_C(0x6D2B79F5) : seed;
  for (int i = totalBlocks - 1; i > 0; i--) {
    const int swapIndex = (int)random_bounded(&randomState, (uint32_t)(i + 1));
    const BlockType type = blockTypes[i];
    blockTypes[i] = blockTypes[swapIndex];
    blockTypes[swapIndex] = type;
  }

  world->blockCount = 0;
  int blockIndex = 0;
  for (int x = 0; x < blocksPerSide; x++) {
    for (int z = 0; z < blocksPerSide; z++) {
      const Block block = {
          .position =
              {
                  .x = center.x +
                       ((float)x - (float)blocksPerSide / 2.0f + 0.5f) * blockSize,
                  .y = center.y - blockSize / 2.0f,
                  .z = center.z +
                       ((float)z - (float)blocksPerSide / 2.0f + 0.5f) * blockSize,
              },
          .size = {.x = blockSize, .y = blockSize, .z = blockSize},
                .type = blockTypes[blockIndex++],
      };
      if (!world_add_block(world, block))
        return 0;
    }
  }
  return 1;
}

static void append_surface_quad(SurfaceVertex *vertices, size_t *cursor,
                                const Vec3 first, const Vec3 second, const Vec3 third,
                                const Vec3 fourth) {
  vertices[(*cursor)++] = (SurfaceVertex){.x = first.x, .y = first.y, .z = first.z};
  vertices[(*cursor)++] = (SurfaceVertex){.x = second.x, .y = second.y, .z = second.z};
  vertices[(*cursor)++] = (SurfaceVertex){.x = third.x, .y = third.y, .z = third.z};
  vertices[(*cursor)++] = (SurfaceVertex){.x = first.x, .y = first.y, .z = first.z};
  vertices[(*cursor)++] = (SurfaceVertex){.x = third.x, .y = third.y, .z = third.z};
  vertices[(*cursor)++] = (SurfaceVertex){.x = fourth.x, .y = fourth.y, .z = fourth.z};
}

int platform_surface_mesh_build(const World *world, const int platformBlocksPerSide,
                                PlatformSurfaceMesh *mesh) {
  if (world == NULL || mesh == NULL || platformBlocksPerSide <= 0)
    return 0;

  const size_t side = (size_t)platformBlocksPerSide;
  if (side > SIZE_MAX / side)
    return 0;
  const size_t expectedBlockCount = side * side;
  if (world->blockCount < 0 ||
      (size_t)world->blockCount != expectedBlockCount)
    return 0;

  size_t quadCounts[BLOCK_TYPE_COUNT] = {0};
  for (size_t index = 0; index < expectedBlockCount; index++) {
    const Block *block = &world->blocks[index];
    if (block->type < BLOCK_TYPE_GRASS || block->type >= BLOCK_TYPE_COUNT)
      return 0;

    quadCounts[block->type]++;
    const size_t x = index / side;
    const size_t z = index % side;
    if (x == 0)
      quadCounts[block->type]++;
    if (x + 1 == side)
      quadCounts[block->type]++;
    if (z == 0)
      quadCounts[block->type]++;
    if (z + 1 == side)
      quadCounts[block->type]++;
  }

  PlatformSurfaceMesh result = {0};
  size_t totalQuads = 0;
  for (int type = 0; type < BLOCK_TYPE_COUNT; type++) {
    if (quadCounts[type] > SIZE_MAX / 6 ||
        totalQuads > SIZE_MAX - quadCounts[type])
      return 0;
    result.firstVertex[type] = totalQuads * 6;
    result.typeVertexCount[type] = quadCounts[type] * 6;
    totalQuads += quadCounts[type];
  }
  if (totalQuads > SIZE_MAX / (6 * sizeof(SurfaceVertex)))
    return 0;
  result.vertexCount = totalQuads * 6;
  result.vertices = malloc(result.vertexCount * sizeof(*result.vertices));
  if (result.vertices == NULL)
    return 0;

  size_t cursors[BLOCK_TYPE_COUNT];
  for (int type = 0; type < BLOCK_TYPE_COUNT; type++)
    cursors[type] = result.firstVertex[type];

  for (size_t index = 0; index < expectedBlockCount; index++) {
    const Block *block = &world->blocks[index];
    const size_t x = index / side;
    const size_t z = index % side;
    const float halfX = block->size.x / 2.0f;
    const float halfY = block->size.y / 2.0f;
    const float halfZ = block->size.z / 2.0f;
    const float left = block->position.x - halfX;
    const float right = block->position.x + halfX;
    const float bottom = block->position.y - halfY;
    const float top = block->position.y + halfY;
    const float back = block->position.z - halfZ;
    const float front = block->position.z + halfZ;
    size_t *cursor = &cursors[block->type];

    append_surface_quad(
        result.vertices, cursor, (Vec3){.x = left, .y = top, .z = back},
        (Vec3){.x = left, .y = top, .z = front}, (Vec3){.x = right, .y = top, .z = front},
        (Vec3){.x = right, .y = top, .z = back});
    if (x == 0)
      append_surface_quad(
          result.vertices, cursor, (Vec3){.x = left, .y = bottom, .z = front},
          (Vec3){.x = left, .y = top, .z = front}, (Vec3){.x = left, .y = top, .z = back},
          (Vec3){.x = left, .y = bottom, .z = back});
    if (x + 1 == side)
      append_surface_quad(
          result.vertices, cursor, (Vec3){.x = right, .y = bottom, .z = back},
          (Vec3){.x = right, .y = top, .z = back}, (Vec3){.x = right, .y = top, .z = front},
          (Vec3){.x = right, .y = bottom, .z = front});
    if (z == 0)
      append_surface_quad(
          result.vertices, cursor, (Vec3){.x = left, .y = bottom, .z = back},
          (Vec3){.x = left, .y = top, .z = back}, (Vec3){.x = right, .y = top, .z = back},
          (Vec3){.x = right, .y = bottom, .z = back});
    if (z + 1 == side)
      append_surface_quad(
          result.vertices, cursor, (Vec3){.x = right, .y = bottom, .z = front},
          (Vec3){.x = right, .y = top, .z = front}, (Vec3){.x = left, .y = top, .z = front},
          (Vec3){.x = left, .y = bottom, .z = front});
  }

  *mesh = result;
  return 1;
}

void platform_surface_mesh_destroy(PlatformSurfaceMesh *mesh) {
  if (mesh == NULL)
    return;
  free(mesh->vertices);
  *mesh = (PlatformSurfaceMesh){0};
}

void update_player(Player *updatedPlayer, const PlayerInput input, const float deltaTime,
                   const World *world) {
  const float moveSpeed = 4.0f;
  const Vec3 frontXZ = vec3_normalize((Vec3){updatedPlayer->camera.front.x, 0.0f,
                                       updatedPlayer->camera.front.z});
  const Vec3 right = vec3_normalize(vec3_cross(frontXZ, updatedPlayer->camera.up));
  Vec3 movement = vec3_add(vec3_scale(frontXZ, input.forward),
                           vec3_scale(right, input.strafe));
  if (vec3_dot(movement, movement) > 1.0f)
    movement = vec3_normalize(movement);

  updatedPlayer->position = vec3_add(
      updatedPlayer->position, vec3_scale(movement, moveSpeed * deltaTime));

  if (updatedPlayer->grounded) {
    int hasSupport = 0;
    for (int i = 0; i < world->blockCount; i++) {
      const Block *block = &world->blocks[i];
      const float halfWidth = block->size.x / 2.0f;
      const float halfDepth = block->size.z / 2.0f;
      const float blockTop = block->position.y + block->size.y / 2.0f;
      if (updatedPlayer->position.x >= block->position.x - halfWidth &&
          updatedPlayer->position.x <= block->position.x + halfWidth &&
          updatedPlayer->position.z >= block->position.z - halfDepth &&
          updatedPlayer->position.z <= block->position.z + halfDepth &&
          fabsf(updatedPlayer->position.y - blockTop) < 0.001f) {
        hasSupport = 1;
        break;
      }
    }
    if (!hasSupport)
      updatedPlayer->grounded = 0;
  }

  if (input.jump && updatedPlayer->grounded) {
    updatedPlayer->velocityY = 5.0f;
    updatedPlayer->grounded = 0;
  }
  if (updatedPlayer->grounded)
    return;

  const float previousY = updatedPlayer->position.y;
  updatedPlayer->velocityY -= GRAVITY * deltaTime;
  updatedPlayer->position.y += updatedPlayer->velocityY * deltaTime;

  for (int i = 0; i < world->blockCount; i++) {
    const Block *block = &world->blocks[i];
    const float halfWidth = block->size.x / 2.0f;
    const float halfDepth = block->size.z / 2.0f;
    const float blockTop = block->position.y + block->size.y / 2.0f;

    if (updatedPlayer->position.x >= block->position.x - halfWidth &&
        updatedPlayer->position.x <= block->position.x + halfWidth &&
        updatedPlayer->position.z >= block->position.z - halfDepth &&
        updatedPlayer->position.z <= block->position.z + halfDepth &&
        previousY >= blockTop && updatedPlayer->position.y <= blockTop) {
      updatedPlayer->position.y = blockTop;
      updatedPlayer->velocityY = 0.0f;
      updatedPlayer->grounded = 1;
      break;
    }
  }
}