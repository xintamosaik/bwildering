/* Frozen pre-issue-005 mesher, for benchmark comparison only.
   Keep world_get_block semantics as the independent query path. */
static int baseline_mesh_build(const World *world, int cx, int sy, int cz, SectionMesh *mesh) {
  const Chunk *chunk = find_chunk(world, cx, cz);
  if (!mesh || !chunk || sy < voxel_section_coord(WORLD_COORD_MIN) ||
      sy > voxel_section_coord(WORLD_COORD_MAX))
    return 0;
  SectionMesh result = {0};
  const Subchunk *section = world_section(world, cx, sy, cz);
  Subchunk *temporary = NULL;
  if (!section && world->proceduralTerrain && !chunk->terrainReady) {
    temporary = terrain_section(world, cx, sy, cz, NULL);
    if (!temporary)
      return 0;
    section = temporary;
  }
  int editedNeighbor = chunk->edited;
  const int neighborX[4] = {-1, 1, 0, 0}, neighborZ[4] = {0, 0, -1, 1};
  for (int d = 0; d < 4; d++) {
    const Chunk *neighbor = find_chunk(world, cx + neighborX[d], cz + neighborZ[d]);
    if (neighbor && neighbor->edited) editedNeighbor = 1;
  }
  if (!section || section->voxelCount == 0 || (!section->meshCandidate && !editedNeighbor)) {
    section_mesh_destroy(mesh);
    *mesh = result;
    free(temporary);
    return 1;
  }
  const int dx[6] = {-1, 1, 0, 0, 0, 0}, dy[6] = {0, 0, -1, 1, 0, 0},
            dz[6] = {0, 0, 0, 0, -1, 1};
  /* Counterclockwise corners viewed from outside: -X,+X,-Y,+Y,-Z,+Z. */
  static const int corners[6][4][3] = {{{0, 0, 1}, {0, 1, 1}, {0, 1, 0}, {0, 0, 0}},
                                       {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}},
                                       {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}},
                                       {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}},
                                       {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}},
                                       {{1, 0, 1}, {1, 1, 1}, {0, 1, 1}, {0, 0, 1}}};
  size_t counts[BLOCK_TYPE_COUNT] = {0};
  /* Two passes retain contiguous per-material draw ranges. */
  for (int pass = 0; pass < 2; pass++) {
    size_t cursors[BLOCK_TYPE_COUNT];
    for (int t = 0; t < BLOCK_TYPE_COUNT; t++)
      cursors[t] = result.firstVertex[t];
    for (int x = 0; x < 16; x++)
      for (int y = 0; y < 16; y++)
        for (int z = 0; z < 16; z++) {
          BlockType type = (BlockType)section->blocks[(x * 16 + y) * 16 + z];
          if (type == BLOCK_TYPE_EMPTY)
            continue;
          if (!block_is_solid(type)) {
            section_mesh_destroy(&result);
            free(temporary);
            return 0;
          }
          int wx = cx * 16 + x, wy = sy * 16 + y, wz = cz * 16 + z;
          for (int d = 0; d < 6; d++) {
            if (!block_face_visible(
                    type, baseline_neighbor(world, wx + dx[d], wy + dy[d], wz + dz[d])))
              continue;
            if (pass == 0) {
              counts[type] += 6;
              continue;
            }
            static const int order[6] = {0, 1, 2, 0, 2, 3};
            for (int v = 0; v < 6; v++) {
              const int *corner = corners[d][order[v]];
              result.vertices[cursors[type]++] =
                  (SurfaceVertex){world->origin.x + (wx + corner[0]) * world->blockSize,
                                  world->origin.y + (wy + corner[1]) * world->blockSize,
                                  world->origin.z + (wz + corner[2]) * world->blockSize};
            }
          }
        }
    if (pass == 0) {
      for (int t = 0; t < BLOCK_TYPE_COUNT; t++) {
        result.firstVertex[t] = result.vertexCount;
        result.typeVertexCount[t] = counts[t];
        result.vertexCount += counts[t];
      }
      /* A section has at most 4096*6*6 vertices. */
      result.vertices = result.vertexCount
                            ? malloc(result.vertexCount * sizeof(*result.vertices))
                            : NULL;
      if (result.vertexCount && !result.vertices) {
        free(temporary);
        return 0;
      }
    }
  }
  section_mesh_destroy(mesh);
  *mesh = result;
  free(temporary);
  return 1;
}
