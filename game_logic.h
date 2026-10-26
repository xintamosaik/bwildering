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
  int crouching;
} Player;

typedef struct {
  float forward;
  float strafe;
  int jump;
  int run;
  int crouch;
} PlayerInput;

typedef enum {
  /* Zero is grass; initialize air storage to BLOCK_TYPE_EMPTY, not zero. */
  BLOCK_TYPE_GRASS,
  BLOCK_TYPE_STONE,
  BLOCK_TYPE_WATER,
  BLOCK_TYPE_SAND,
  BLOCK_TYPE_DIRT,
  BLOCK_TYPE_GRAVEL,
  BLOCK_TYPE_DARK_STONE,
  BLOCK_TYPE_SNOW,
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
} SectionMesh;

enum {
  PLATFORM_BLOCKS_PER_SIDE = 4608,
  WORLD_MAX_BLOCKS_PER_SIDE = 6144,
  SECTION_SIZE = 16,
  SECTION_VOLUME = 16 * 16 * 16,
  WORLD_MIN_SECTION_Y = -8,
  WORLD_SECTION_COUNT = 32,
  TERRAIN_BASE_Y = -128,
  TERRAIN_SEA_LEVEL = 0,
  TERRAIN_DEEP_ROCK_Y = -64,
  TERRAIN_SHORE_MIN = -20, TERRAIN_SHORE_MAX = 5,
  TERRAIN_STONE_Y = 160, TERRAIN_SNOW_Y = 260,
  TERRAIN_SOIL_MIN = 3, TERRAIN_SOIL_MAX = 6,
  TERRAIN_SAND_MIN = 2, TERRAIN_SAND_MAX = 5,
  TERRAIN_GRAVEL_MIN = 1, TERRAIN_GRAVEL_MAX = 3,
  TERRAIN_SNOW_MIN = 1, TERRAIN_SNOW_MAX = 3,
  TERRAIN_SURFACE_MIN = -85, TERRAIN_SURFACE_MAX = 370,
  /* Horizontal feature widths in blocks; vertical relief is independent. */
  TERRAIN_RELIEF_SCALE = 256,
  TERRAIN_MOUNTAIN_REGION_SCALE = 512,
  TERRAIN_RIDGE_SCALE = 128,
  TERRAIN_HILL_SCALE = 32,
  TERRAIN_DETAIL_SCALE = 8,
  /* Bounded storage leaves ample room for section corners and neighbours. */
  WORLD_COORD_MIN = -1048575,
  WORLD_COORD_MAX = 1048575
};

typedef struct Subchunk {
  int meshCandidate; /* Conservative surface/edge exposure, or an edited section. */
  int sectionY;
  struct Subchunk *next;
  uint8_t blocks[SECTION_VOLUME];
  size_t voxelCount;
  int dirty;
} Subchunk;

typedef struct {
  int x, z;
  Subchunk *sections; /* Sorted by signed sectionY; includes empty edit markers. */
  int terrainReady;
  int edited;
} Chunk;

/* Owning type: initialize to zero, destroy explicitly, and do not copy by value.
   Missing sections read as air in ordinary worlds; streamed worlds synthesize
   missing chunks from their seed. Horizontal side is 1..WORLD_MAX_BLOCKS_PER_SIDE;
   generated voxel Y is [-128,384); edits use WORLD_COORD_MIN..MAX.
   Empty allocated sections remain owned retirement markers until destruction;
   sectionCount includes them. Renderer sync must retire their GPU geometry.
   Voxel coordinates are signed; render positions use origin + voxel * blockSize. */
typedef struct {
  Chunk *chunks;
  size_t chunkCount;
  size_t sectionCount;
  size_t voxelCount;
  int minChunkX, minChunkZ, chunksPerSide;
  int minX, minZ, blocksPerSide;
  Vec3 origin;
  float blockSize;
  /* Streamed terrain synthesizes missing chunks. Counts describe resident data.
     Edited chunks stay resident; unedited chunks can be regenerated after eviction. */
  int proceduralTerrain;
  uint32_t terrainSeed;
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
/* Positive divisor required; valid for every int coordinate. */
int voxel_floor_div(int coordinate, int divisor);
int voxel_floor_mod(int coordinate, int divisor);
int voxel_section_coord(int coordinate);
int voxel_local_coord(int coordinate);
int block_is_solid(BlockType type);
int block_face_visible(BlockType type, BlockType neighbor);
/* Initialization/generation replace an existing initialized world only on success.
   side is 1..WORLD_MAX_BLOCKS_PER_SIDE; blockSize must be finite and >= 0.001 world units, and
   individual cells must be representable by floats throughout the world.
   center.x/z center the footprint; center.y is the voxel Y=0 reference plane. */
int world_init(World *world, int side, Vec3 center, float blockSize);
/* Explicit square voxel bounds [minX,minX+side), [minZ,minZ+side).
   Every stored X/Z must be within WORLD_COORD_MIN..WORLD_COORD_MAX.
   origin is the render/physics position of voxel (0,0,0), not the region center.
   Same ownership, size and float representability rules as world_init. */
int world_init_region(World *world, int minX, int minZ, int side,
                      Vec3 origin, float blockSize);
void world_destroy(World *world);
/* Borrowed section access; both return NULL for missing or out-of-range sections. */
const Subchunk *world_section(const World *world, int chunkX, int sectionY, int chunkZ);
Subchunk *world_section_mut(World *world, int chunkX, int sectionY, int chunkZ);
BlockType world_get_block(const World *world, int x, int y, int z);
/* Returns success, including identical writes; rejects invalid types/bounds.
   Writes allocate missing sections lazily, initialized to air or seeded terrain.
   Successful edits update CPU data/counts immediately, so collision sees them
   before rendering catches up. Identical writes leave dirty flags unchanged.
   Edits dirty this section and existing sections sharing an affected face;
   missing neighbours are not allocated. Last-voxel removal retains an empty
   section so renderer_sync_world() can retire its GPU resources.
   GPU replacement is transactional per section during renderer synchronization:
   failure retains that section's old mesh and dirty flag for retry, but earlier
   sections in the pass may already have updated. CPU edits are not rolled back. */
int world_set_block(World *world, int x, int y, int z, BlockType type);
/* Highest occupied surface, for spawn selection (not cave collision). */
float world_surface_height(const World *world, float x, float z);
/* Returns a value in [0, upperBound). NULL state or zero bound returns zero
   without advancing state. For positive bounds, a zero state is replaced with
   0x6D2B79F5 before advancing the generator. */
uint32_t random_bounded(uint32_t *state, uint32_t upperBound);
int block_type_color(BlockType type, Vec3 *color);
int create_platform(World *world, int blocksPerSide, Vec3 center,
                    float blockSize, uint32_t seed);
typedef struct {
  int height; /* Filled voxels are TERRAIN_BASE_Y through height-1. */
  BlockType material; /* Highest occupied block. */
  int deepRockY, soilDepth, sandDepth, gravelDepth, snowDepth;
} TerrainColumn;

/* Pure signed voxel sampling, independent of bounds and render transform.
   Sampler accepts every int X/Z. Noise supports powers of two from 8 to 2048. */
float terrain_noise(int worldX, int worldZ, int scale, uint32_t seed);
TerrainColumn terrain_sample_column(uint32_t seed, int worldX, int worldZ);
BlockType terrain_column_block(TerrainColumn column, int y);
/* Transactional bounded region generation; bounds/transform as world_init_region. */
int create_terrain_region(World *world, int minX, int minZ, int side,
                          Vec3 origin, float blockSize, uint32_t seed);
/* Seeded heightfield with signed basins, mountain regions and shallow layers. */
int create_terrain(World *world, int blocksPerSide, Vec3 center,
                   float blockSize, uint32_t seed);
/* Same terrain and bounds, generated on demand rather than all at startup. */
int create_terrain_streamed(World *world, int blocksPerSide, Vec3 center,
                            float blockSize, uint32_t seed);
/* Transactionally fill missing sections, preserving edits already in this chunk. */
int world_materialize_chunk(World *world, int chunkX, int chunkZ);
/* Evict only unedited procedural chunks; subsequent reads regenerate their values. */
void world_release_chunk(World *world, int chunkX, int chunkZ);
/* Transactionally replaces an initialized CPU mesh. Empty sections are valid. */
int section_mesh_build(const World *world, int chunkX, int sectionY, int chunkZ,
                       SectionMesh *mesh);
void section_mesh_destroy(SectionMesh *mesh);
void update_player(Player *updatedPlayer, PlayerInput input, float deltaTime,
                   const World *world);
/* Effective camera position, including the held crouch's 0.3-unit offset. */
Vec3 player_eye_position(const Player *player);
/* Place feet above all terrain overlapping the body footprint. */
int player_spawn_on_surface(Player *player, const World *world);
/* Place against the first block along the view ray, within reach in world units.
   Rejects misses, occupied eyes, invalid materials, and overlap with the player. */
int player_place_block(World *world, const Player *player, BlockType type, float reach);
/* Remove the first block along the view ray; misses leave the world unchanged. */
int player_remove_block(World *world, const Player *player, float reach);

#endif
