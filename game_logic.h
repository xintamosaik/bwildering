#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

#include <stddef.h>
#include <stdint.h>
#include "block_type.h"

typedef struct {
  float x;
  float y;
  float z;
} Vec3;

typedef struct {
  float yaw;
  float pitch;
  Vec3 front;
} GameplayAim;

typedef struct {
  Vec3 position;
  float velocityY;
  int grounded;
  float eyeHeight;
  GameplayAim aim;
  int crouching;
} Player;

typedef struct {
  float forward;
  float strafe;
  int jump; /* Pressed once for a land jump. */
  int swimUp; /* Held continuously in water; does not repeat land jumps. */
  int run;
  int crouch; /* Held: crouch on land, dive in water. */
} PlayerInput;

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
  /* Six faces, two triangles, three vertices; valid for every voxel pattern. */
  SECTION_MESH_MAX_VERTICES = SECTION_VOLUME * 6 * 6,
  WORLD_EDIT_CAPACITY = 65536,
  WORLD_MIN_SECTION_Y = -8,
  WORLD_SECTION_COUNT = 32,
  /* Bounded storage leaves ample room for section corners and neighbours. */
  WORLD_COORD_MIN = -1048575,
  WORLD_COORD_MAX = 1048575
};

typedef enum { SECTION_EMPTY, SECTION_UNIFORM, SECTION_DENSE } SectionStorage;

/* Owned by its chunk; blocks is owned only by this section, never shared. */
struct WorldStorage;
typedef struct Subchunk {
  struct WorldStorage *pool; /* Stable world-owned pool, including staged nodes. */
  int meshCandidate; /* Conservative surface/edge exposure, or an edited section. */
  int sectionY;
  struct Subchunk *next;
  SectionStorage storage;
  uint8_t material; /* EMPTY for empty sections; solid for uniform sections. */
  uint8_t *blocks; /* Owned SECTION_VOLUME bytes only in SECTION_DENSE. */
  size_t voxelCount;
  int dirty;
  uint64_t revision; /* Changes when voxel data or a meshing dependency changes. */
} Subchunk;

/* index must be in [0, SECTION_VOLUME). Missing sections read as air. */
static inline BlockType section_block(const Subchunk *const section, const size_t index) {
  return !section || section->storage == SECTION_EMPTY ? BLOCK_TYPE_EMPTY :
      (BlockType)(section->storage == SECTION_UNIFORM ? section->material : section->blocks[index]);
}

/* One world-owned arena; indices are one-based, zero terminates sparse lists. */
typedef struct {
  int x, y, z;
  uint32_t next;
  uint8_t material; /* Includes explicit EMPTY removals. */
} VoxelEdit;

typedef struct {
  int x, z;
  Subchunk *sections; /* Sorted by signed sectionY; includes empty edit markers. */
  int terrainReady;
  int edited; /* Overlay presence, independent of resident sections. */
  uint32_t editHead;
} Chunk;

/* Owning type: initialize to zero, destroy explicitly, and do not copy by value.
   Missing sections read as air in ordinary worlds; streamed worlds synthesize
   missing chunks from their seed. Horizontal side is 1..WORLD_MAX_BLOCKS_PER_SIDE;
   generated solids occupy [-128,-32), water [-32,1); edits use WORLD_COORD_MIN..MAX.
   Empty allocated sections remain owned retirement markers until CPU eviction
   or destruction;
   sectionCount includes them. Renderer sync must retire their GPU geometry.
   Voxel coordinates are signed; render positions use origin + voxel * blockSize. */
typedef struct {
  struct WorldStorage *storage;
  Chunk *chunks;
  VoxelEdit *edits; /* Lazily reserved WORLD_EDIT_CAPACITY-entry arena. */
  size_t editCount;
  uint32_t editUsed, editFree;
  size_t chunkCount;
  size_t sectionCount;
  size_t voxelCount;
  int minChunkX, minChunkZ, chunksPerSide;
  int minX, minZ, blocksPerSide;
  Vec3 origin;
  float blockSize;
  /* Streamed terrain synthesizes missing chunks. Counts describe resident data.
     Overlay entries retain only differences from the seed, including air.
     Reads prefer resident sections, then overlay, then baseline. Edits are lost
     on destruction; no disk persistence. A full pool rejects new differences
     transactionally (world_set_block returns zero); restoration reclaims slots. */
  int proceduralTerrain;
  uint32_t terrainSeed;
  int hasOcean;
  int waterTopY; /* Exclusive water top in voxel coordinates for the flat ocean. */
  /* Conservative envelope of resident columns, including partial edit caches. */
  int hasMaterializedBounds;
  int minMaterializedChunkX, maxMaterializedChunkX;
  int minMaterializedChunkZ, maxMaterializedChunkZ;
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
/* Material validity includes water; solid blocks alone support/collide with players. */
int block_is_material(BlockType type);
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
Subchunk *world_section_mut(const World *world, int chunkX, int sectionY, int chunkZ);
BlockType world_get_block(const World *world, int x, int y, int z);
/* Returns success, including identical writes; rejects invalid types/bounds.
   Writes allocate missing sections lazily, initialized to air or seeded terrain.
   Allocation failure leaves section ownership, values and metadata unchanged.
   Successful edits update CPU data/counts immediately, so collision sees them
   before rendering catches up. Identical writes leave dirty flags unchanged.
   Edits dirty this section and existing sections sharing an affected face;
   missing neighbours are not allocated. Last-voxel removal retains an empty
   section so the cache consumer can retire its GPU resources.
   GPU replacement is transactional per section during renderer synchronization:
   failure retains that section's old mesh and dirty flag for retry, but earlier
   sections in the pass may already have updated. CPU edits are not rolled back. */
int world_set_block(World *world, int x, int y, int z, BlockType type);
/* Highest occupied surface, including water (not cave collision). */
float world_surface_height(const World *world, float x, float z);
/* Highest solid surface, ignoring water; used for grounded spawning. */
float world_support_height(const World *world, float x, float z);
int block_type_color(BlockType type, Vec3 *color);
/* Transactionally fill missing sections, preserving edits already in this chunk. */
int world_materialize_chunk(World *world, int chunkX, int chunkZ);
/* Evict procedural CPU caches; subsequent reads use seed plus session overlay.
   Caller must retire GPU resources and cancel scheduled work before recycling. */
void world_release_chunk(World *world, int chunkX, int chunkZ);
/* Release procedural columns outside inclusive residency bounds (min > max is
   an empty neighbourhood). Tracks actual
   materialization independently of renderer success; unchanged bounds cost O(1).
   Returns the number of previously resident columns released. */
size_t world_release_chunks_outside(World *world, int minX, int maxX, int minZ, int maxZ);
/* Transactionally replaces an initialized CPU mesh. Empty sections are valid. */
int section_mesh_build(const World *world, int chunkX, int sectionY, int chunkZ,
                       SectionMesh *mesh);
/* Output must be a nonowning descriptor. Borrowed vertices remain valid until
   the caller reuses scratch; never destroy this output.
   Overflow/failure leaves output unchanged. Capacity is in vertices. */
int section_mesh_build_scratch(const World *world, int cx, int sy, int cz,
                               SurfaceVertex *scratch, size_t capacity, SectionMesh *output);
/* Configure before acquiring any nodes; zero is a valid exhaustion limit.
   Default: all generated columns + maximum overlay sections + one staged column.
   Pages are committed lazily and retained until world destruction. */
int world_storage_limits(World *world, size_t nodes, size_t payloads);
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
