#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

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

typedef struct {
  Vec3 position;
  Vec3 size;
} Block;

enum { WORLD_MAX_BLOCKS = 1024, PLATFORM_BLOCKS_PER_SIDE = 32 };

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
void mat4_multiply(mat4 a, mat4 b, mat4 dest);
int world_add_block(World *world, Block block);
int create_platform(World *world, int blocksPerSide, Vec3 center,
                    float blockSize);
void update_player(Player *updatedPlayer, PlayerInput input, float deltaTime,
                   const World *world);

#endif