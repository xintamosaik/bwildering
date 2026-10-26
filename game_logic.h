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
  BLOCK_TYPE_COUNT
} BlockType;

typedef struct {
  Vec3 position;
  Vec3 size;
  BlockType type;
} Block;

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
  Block blocks[WORLD_MAX_BLOCKS];
  int blockCount;
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
int world_add_block(World *world, Block block);
uint32_t random_bounded(uint32_t *state, uint32_t upperBound);
int block_type_color(BlockType type, Vec3 *color);
int create_platform(World *world, int blocksPerSide, Vec3 center,
                    float blockSize, uint32_t seed);
/* Builds the top and perimeter faces of a complete, regularly ordered
  platform from create_platform(); this is not a general voxel-world mesher.
  Blocks must be stored x-major/z-minor and total blocksPerSide squared. */
int platform_surface_mesh_build(const World *world, int platformBlocksPerSide,
                                PlatformSurfaceMesh *mesh);
void platform_surface_mesh_destroy(PlatformSurfaceMesh *mesh);
void update_player(Player *updatedPlayer, PlayerInput input, float deltaTime,
                   const World *world);

#endif