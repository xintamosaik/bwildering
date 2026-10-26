/* Top-down view of the actual column sampler, without an OpenGL context. */
#include "../game_logic.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
  uint32_t seed = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 10) : 1;
  int side = argc > 2 ? atoi(argv[2]) : PLATFORM_BLOCKS_PER_SIDE;
  if (side < PLATFORM_BLOCKS_PER_SIDE || side > WORLD_MAX_BLOCKS_PER_SIDE) return 1;
  Landmass land = terrain_landmass(seed, side);
  char path[128];
  snprintf(path, sizeof(path), "/tmp/landmass-%u-%d.ppm", seed, side);
  FILE *file = fopen(path, "wb");
  if (!file) return 1;
  fprintf(file, "P6\n512 512\n255\n");
  size_t landCount = 0;
  for (int z = 0; z < 512; z++)
    for (int x = 0; x < 512; x++) {
      int wx = -side / 2 + (int)((x + 0.5f) * side / 512);
      int wz = -side / 2 + (int)((z + 0.5f) * side / 512);
      TerrainColumn c = terrain_sample_landmass(seed, wx, wz, &land);
      Vec3 color;
      block_type_color(c.height > 0 ? c.material : BLOCK_TYPE_WATER, &color);
      float shade = c.height > 0 ? 0.7f + 0.3f * c.height / 370 : 0.75f + 0.25f * (80 + c.height) / 80;
      landCount += c.height > 0;
      unsigned char rgb[3] = {(unsigned char)(color.x * shade * 255),
                              (unsigned char)(color.y * shade * 255),
                              (unsigned char)(color.z * shade * 255)};
      if (fwrite(rgb, 1, 3, file) != 3) return 1;
    }
  fclose(file);
  printf("seed %u side %d: %.2f%% land, %.2f%% ocean -> %s\n", seed, side,
         100.0 * landCount / (512 * 512), 100.0 * (512 * 512 - landCount) / (512 * 512), path);
  return 0;
}
