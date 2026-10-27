#ifndef BLOCK_TYPE_H
#define BLOCK_TYPE_H

#include <stdint.h>

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

#endif
