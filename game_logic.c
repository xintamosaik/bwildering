#include "game_logic.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>

static const float GRAVITY = 9.81f;
static const Vec3 BLOCK_TYPE_COLORS[BLOCK_TYPE_COUNT] = {
  [BLOCK_TYPE_GRASS] = {0.38f, 0.62f, 0.42f},
  [BLOCK_TYPE_STONE] = {0.55f, 0.58f, 0.60f},
  [BLOCK_TYPE_WATER] = {0.20f, 0.48f, 0.78f},
  [BLOCK_TYPE_SAND] = {0.86f, 0.72f, 0.42f},
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

  uint32_t threshold = (0u - upperBound) % upperBound;
  uint32_t value;
  do {
    value = next_random(state);
  } while (value < threshold);

  return value % upperBound;
}

Vec3 vec3_add(Vec3 a, Vec3 b) {
  return (Vec3){a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 vec3_sub(Vec3 a, Vec3 b) {
  return (Vec3){a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 vec3_scale(Vec3 v, float scalar) {
  return (Vec3){v.x * scalar, v.y * scalar, v.z * scalar};
}

Vec3 vec3_normalize(Vec3 v) {
  float length = sqrtf(vec3_dot(v, v));
  if (length == 0.0f)
    return (Vec3){0.0f, 0.0f, 0.0f};
  return vec3_scale(v, 1.0f / length);
}

Vec3 vec3_cross(Vec3 a, Vec3 b) {
  return (Vec3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x};
}

float vec3_dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

int block_type_color(BlockType type, Vec3 *color) {
  if (color == NULL || type < BLOCK_TYPE_GRASS || type >= BLOCK_TYPE_COUNT)
    return 0;

  *color = BLOCK_TYPE_COLORS[type];
  return 1;
}

void mat4_rotate(mat4 dest, float angle, float x, float y, float z) {
  float c = cosf(angle);
  float s = sinf(angle);
  float len = sqrtf(x * x + y * y + z * z);
  if (len == 0.0f) {
    mat4_identity(dest);
    return;
  }
  x /= len;
  y /= len;
  z /= len;
  float ic = 1.0f - c;

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

void mat4_perspective(float fov_radians, float aspect, float near, float far,
                      mat4 dest) {
  for (int i = 0; i < 16; i++)
    dest[i] = 0.0f;
  float tanHalfFovy = tanf(fov_radians / 2.0f);

  dest[0] = 1.0f / (aspect * tanHalfFovy);
  dest[5] = 1.0f / tanHalfFovy;
  dest[10] = -(far + near) / (far - near);
  dest[11] = -1.0f;
  dest[14] = -(2.0f * far * near) / (far - near);
}

void mat4_lookat(Vec3 eye, Vec3 target, Vec3 up, mat4 dest) {
  Vec3 f = vec3_normalize(vec3_sub(target, eye));
  Vec3 s = vec3_normalize(vec3_cross(f, up));
  Vec3 u = vec3_cross(s, f);

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

void mat4_multiply(mat4 a, mat4 b, mat4 dest) {
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

int create_platform(World *world, int blocksPerSide, Vec3 center,
                    float blockSize, uint32_t seed) {
  if (world == NULL || blocksPerSide <= 0 ||
      blocksPerSide > WORLD_MAX_BLOCKS / blocksPerSide)
    return 0;

  int totalBlocks = blocksPerSide * blocksPerSide;
  BlockType blockTypes[WORLD_MAX_BLOCKS];
  for (int i = 0; i < totalBlocks; i++)
    blockTypes[i] = (BlockType)(i % BLOCK_TYPE_COUNT);

  uint32_t randomState = seed == 0 ? UINT32_C(0x6D2B79F5) : seed;
  for (int i = totalBlocks - 1; i > 0; i--) {
    int swapIndex = (int)random_bounded(&randomState, (uint32_t)(i + 1));
    BlockType type = blockTypes[i];
    blockTypes[i] = blockTypes[swapIndex];
    blockTypes[swapIndex] = type;
  }

  world->blockCount = 0;
  int blockIndex = 0;
  for (int x = 0; x < blocksPerSide; x++) {
    for (int z = 0; z < blocksPerSide; z++) {
      Block block = {
          .position =
              {
                  .x = center.x +
                       ((float)x - blocksPerSide / 2.0f + 0.5f) * blockSize,
                  .y = center.y - blockSize / 2.0f,
                  .z = center.z +
                       ((float)z - blocksPerSide / 2.0f + 0.5f) * blockSize,
              },
          .size = {blockSize, blockSize, blockSize},
                .type = blockTypes[blockIndex++],
      };
      if (!world_add_block(world, block))
        return 0;
    }
  }
  return 1;
}

static void append_surface_quad(SurfaceVertex *vertices, size_t *cursor,
                                Vec3 first, Vec3 second, Vec3 third,
                                Vec3 fourth) {
  vertices[(*cursor)++] = (SurfaceVertex){first.x, first.y, first.z};
  vertices[(*cursor)++] = (SurfaceVertex){second.x, second.y, second.z};
  vertices[(*cursor)++] = (SurfaceVertex){third.x, third.y, third.z};
  vertices[(*cursor)++] = (SurfaceVertex){first.x, first.y, first.z};
  vertices[(*cursor)++] = (SurfaceVertex){third.x, third.y, third.z};
  vertices[(*cursor)++] = (SurfaceVertex){fourth.x, fourth.y, fourth.z};
}

int platform_surface_mesh_build(const World *world, int platformBlocksPerSide,
                                PlatformSurfaceMesh *mesh) {
  if (world == NULL || mesh == NULL || platformBlocksPerSide <= 0)
    return 0;

  size_t side = (size_t)platformBlocksPerSide;
  if (side > SIZE_MAX / side)
    return 0;
  size_t expectedBlockCount = side * side;
  if (world->blockCount < 0 ||
      (size_t)world->blockCount != expectedBlockCount)
    return 0;

  size_t quadCounts[BLOCK_TYPE_COUNT] = {0};
  for (size_t index = 0; index < expectedBlockCount; index++) {
    const Block *block = &world->blocks[index];
    if (block->type < BLOCK_TYPE_GRASS || block->type >= BLOCK_TYPE_COUNT)
      return 0;

    quadCounts[block->type]++;
    size_t x = index / side;
    size_t z = index % side;
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
    size_t x = index / side;
    size_t z = index % side;
    float halfX = block->size.x / 2.0f;
    float halfY = block->size.y / 2.0f;
    float halfZ = block->size.z / 2.0f;
    float left = block->position.x - halfX;
    float right = block->position.x + halfX;
    float bottom = block->position.y - halfY;
    float top = block->position.y + halfY;
    float back = block->position.z - halfZ;
    float front = block->position.z + halfZ;
    size_t *cursor = &cursors[block->type];

    append_surface_quad(
        result.vertices, cursor, (Vec3){left, top, back},
        (Vec3){left, top, front}, (Vec3){right, top, front},
        (Vec3){right, top, back});
    if (x == 0)
      append_surface_quad(
          result.vertices, cursor, (Vec3){left, bottom, front},
          (Vec3){left, top, front}, (Vec3){left, top, back},
          (Vec3){left, bottom, back});
    if (x + 1 == side)
      append_surface_quad(
          result.vertices, cursor, (Vec3){right, bottom, back},
          (Vec3){right, top, back}, (Vec3){right, top, front},
          (Vec3){right, bottom, front});
    if (z == 0)
      append_surface_quad(
          result.vertices, cursor, (Vec3){left, bottom, back},
          (Vec3){left, top, back}, (Vec3){right, top, back},
          (Vec3){right, bottom, back});
    if (z + 1 == side)
      append_surface_quad(
          result.vertices, cursor, (Vec3){right, bottom, front},
          (Vec3){right, top, front}, (Vec3){left, top, front},
          (Vec3){left, bottom, front});
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

void update_player(Player *updatedPlayer, PlayerInput input, float deltaTime,
                   const World *world) {
  const float moveSpeed = 4.0f;
  Vec3 frontXZ = vec3_normalize((Vec3){updatedPlayer->camera.front.x, 0.0f,
                                       updatedPlayer->camera.front.z});
  Vec3 right = vec3_normalize(vec3_cross(frontXZ, updatedPlayer->camera.up));
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
      float halfWidth = block->size.x / 2.0f;
      float halfDepth = block->size.z / 2.0f;
      float blockTop = block->position.y + block->size.y / 2.0f;
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

  float previousY = updatedPlayer->position.y;
  updatedPlayer->velocityY -= GRAVITY * deltaTime;
  updatedPlayer->position.y += updatedPlayer->velocityY * deltaTime;

  for (int i = 0; i < world->blockCount; i++) {
    const Block *block = &world->blocks[i];
    float halfWidth = block->size.x / 2.0f;
    float halfDepth = block->size.z / 2.0f;
    float blockTop = block->position.y + block->size.y / 2.0f;

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