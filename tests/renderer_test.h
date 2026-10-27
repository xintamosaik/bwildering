#ifndef RENDERER_TEST_H
#define RENDERER_TEST_H

/* Fixture-only observations; compile renderer.c with BW_RENDERER_TEST.
   Production helpers remain private. GL interception uses the linker wrapper. */
#include "../application_internal.h"
#include "../storage.h"
#include "../view_settings.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

GLuint compile_shader(const GLenum type, const char *const source);
GLuint create_shader_program(void);
void section_gpu_mesh_destroy(SectionGpuMesh *const mesh);
int renderer_sync_world(Renderer *const renderer, const World *const world);
const Chunk *stream_chunk(const World *const world, const int x, const int z);
int stream_identity_compare(const void *const a, const void *const b);
StreamResult renderer_sync_view_work(Renderer *const renderer, World *const world, const Player *const player,
                                            const StreamBudget *const budget);
int section_in_frustum(const SectionGpuMesh *const mesh, const float mvp[const 16]);
int section_within_view_distance(const SectionGpuMesh *const mesh, const Vec3 eye,
                                         const float distance);
int renderer_collect_draw_meshes(Renderer *const renderer, const Vec3 eye,
                                        const float distance, const float mvp[const 16]);

#endif
