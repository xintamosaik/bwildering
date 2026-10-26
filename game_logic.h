#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  float x;
  float y;
  float z;
} Vec3;

typedef struct {
  float yaw;
  float pitch;
  Vec3 front;
  Vec3 up;
} Camera;

typedef struct {
  Vec3 position;
  float velocityY;
  int grounded;
  float eyeHeight;
  Camera camera;
} Player;

typedef struct {
  float forward;
  float strafe;
  int jump;
} PlayerInput;

typedef enum {
  BLOCK_TYPE_GRASS,
  BLOCK_TYPE_STONE,
  BLOCK_TYPE_WATER,
  BLOCK_TYPE_SAND,
  BLOCK_TYPE_COUNT,
  BLOCK_TYPE_EMPTY = UINT8_MAX
} BlockType;

typedef struct {
  float x;
  float y;
  float z;
} SurfaceVertex;

typedef struct {
  SurfaceVertex *vertices;
  size_t vertexCount;
  size_t firstVertex[BLOCK_TYPE_COUNT];
  size_t typeVertexCount[BLOCK_TYPE_COUNT];
} PlatformSurfaceMesh;

enum {
  PLATFORM_BLOCKS_PER_SIDE = 192,
  WORLD_MAX_BLOCKS = PLATFORM_BLOCKS_PER_SIDE * PLATFORM_BLOCKS_PER_SIDE
};

typedef struct {
  uint8_t tiles[WORLD_MAX_BLOCKS];
  uint8_t elevations[WORLD_MAX_BLOCKS]; /* Top height in block units above center.y. */
  int blockCount; /* Occupied cells, not the grid capacity. */
  int blocksPerSide;
  Vec3 center; /* Horizontal center and reference top height (elevation zero). */
  float blockSize;
} World;

typedef float mat4[16];

Vec3 vec3_add(Vec3 a, Vec3 b);
Vec3 vec3_sub(Vec3 a, Vec3 b);
Vec3 vec3_scale(Vec3 v, float scalar);
Vec3 vec3_normalize(Vec3 v);
Vec3 vec3_cross(Vec3 a, Vec3 b);
float vec3_dot(Vec3 a, Vec3 b);
void mat4_identity(mat4 dest);
void mat4_perspective(float fov_radians, float aspect, float near, float far,
                      mat4 dest);
void mat4_lookat(Vec3 eye, Vec3 target, Vec3 up, mat4 dest);
void mat4_rotate(mat4 dest, float angle, float x, float y, float z);
void mat4_multiply(const mat4 a, const mat4 b, mat4 dest);
/* Grid storage is x-major/z-minor. Invalid coordinates map to -1/EMPTY.
   Each occupied cell is a solid column from center.y - blockSize to its top. */
int world_index(int x, int z, int side);
/* Requires a valid world and in-range coordinates.
   Returns the center of the column's topmost cube. */
Vec3 world_cell_to_position(const World *world, int x, int z);
BlockType world_block_at(const World *world, int x, int z);
int world_is_solid(const World *world, int x, int z);
/* Returns the surface under a world-space point, or NAN outside occupied terrain. */
float world_surface_height(const World *world, float x, float z);
/* Use this setter to keep blockCount consistent when editing occupancy. */
int world_set_block(World *world, int x, int z, BlockType type);
uint32_t random_bounded(uint32_t *state, uint32_t upperBound);
int block_type_color(BlockType type, Vec3 *color);
int create_platform(World *world, int blocksPerSide, Vec3 center,
                    float blockSize, uint32_t seed);
/* Seeded, smooth terraced hills with elevation-dependent surface materials. */
int create_terrain(World *world, int blocksPerSide, Vec3 center,
                   float blockSize, uint32_t seed);
/* Builds exposed tops and sides, including sides around empty cells; no bottoms.
   platformBlocksPerSide must match the world's grid size.
   Pass an empty/destroyed mesh; failure leaves it unchanged. */
int platform_surface_mesh_build(const World *world, int platformBlocksPerSide,
                                PlatformSurfaceMesh *mesh);
void platform_surface_mesh_destroy(PlatformSurfaceMesh *mesh);
void update_player(Player *updatedPlayer, PlayerInput input, float deltaTime,
                   const World *world);

#endif
