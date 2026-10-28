CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
LDFLAGS = -lGLEW -lglfw -lGL -lm -lpthread

TARGET = bwildering
COMMON_SRC = world_streaming.c game.c game_logic.c world_generation.c voxel_world.c storage.c performance.c ui.c input.c renderer.c distant_terrain.c
SRC = main.c application.c $(COMMON_SRC)
APP_HEADERS = world_streaming.h game.h view.h application.h application_internal.h renderer.h renderer_internal.h distant_terrain.h water_shader.h ui_draw.h view_settings.h ui.h input.h performance.h storage.h game_logic.h world_generation.h block_type.h
FIXTURE_FLAGS = -DBW_RENDERER_TEST
# Headless routing fixture links production modules and captures registered callbacks.
PAUSE_WRAP = -Wl,--wrap=glfwSetFramebufferSizeCallback -Wl,--wrap=glfwSetWindowUserPointer -Wl,--wrap=glfwSetMouseButtonCallback -Wl,--wrap=glfwSetCursorPosCallback -Wl,--wrap=glfwSetKeyCallback -Wl,--wrap=glfwGetKey -Wl,--wrap=glfwGetMouseButton -Wl,--wrap=glfwGetInputMode -Wl,--wrap=glfwSetInputMode -Wl,--wrap=glfwGetTime -Wl,--wrap=glfwGetWindowUserPointer -Wl,--wrap=glfwGetCursorPos -Wl,--wrap=glfwGetWindowSize -Wl,--wrap=glfwGetFramebufferSize -Wl,--wrap=glfwSetWindowShouldClose -Wl,--wrap=glfwPollEvents
TEST_TARGET = test_game_logic
TEST_SRC = tests/test_game_logic.c game_logic.c world_generation.c voxel_world.c storage.c
BEACH_TEST_SRC = tests/test_beach_generation.c game.c game_logic.c world_generation.c voxel_world.c storage.c

all: $(TARGET)

$(TARGET): $(SRC) $(APP_HEADERS)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

$(TEST_TARGET): $(TEST_SRC) game_logic.h world_generation.h block_type.h storage.h storage.c
	$(CC) $(CFLAGS) $(TEST_SRC) -o $(TEST_TARGET) -lm

test_allocation_failures: tests/test_allocation_failures.c game_logic.c world_generation.c voxel_world.c game_logic.h world_generation.h block_type.h storage.h storage.c
	$(CC) $(CFLAGS) tests/test_allocation_failures.c game_logic.c world_generation.c voxel_world.c storage.c -o $@ -lm

test_renderer: tests/test_renderer.c $(SRC) $(APP_HEADERS) tests/renderer_test.h
	$(CC) $(CFLAGS) tests/test_renderer.c application.c $(COMMON_SRC) $(FIXTURE_FLAGS) -Wl,--wrap=glDrawArrays -Wl,--wrap=glfwGetWindowSize -Wl,--wrap=glfwGetFramebufferSize -Wl,--wrap=glViewport -o $@ $(LDFLAGS)

test_pause_menu: tests/test_pause_menu.c $(SRC) $(APP_HEADERS) tests/renderer_test.h
	$(CC) $(CFLAGS) tests/test_pause_menu.c application.c $(COMMON_SRC) $(FIXTURE_FLAGS) $(PAUSE_WRAP) -o $@ $(LDFLAGS)

test_beach_generation: $(BEACH_TEST_SRC) game.h game_logic.h world_generation.h block_type.h storage.h
	$(CC) $(CFLAGS) $(BEACH_TEST_SRC) -o $@ -lm

test: test_beach_generation test_world_streaming test_game $(TEST_TARGET) test_allocation_failures test_pause_menu test_storage
	./test_beach_generation
	./test_world_streaming
	./test_game
	./$(TEST_TARGET)
	./test_allocation_failures
	./test_pause_menu
	./test_storage

# Requires a local X11 display and an OpenGL 3.3 context; uses an invisible window.
test-renderer: test_renderer
	./test_renderer

test-sanitize:
	$(CC) -Wall -Wextra -O1 -g -std=c99 -fsanitize=address,undefined -fno-omit-frame-pointer $(BEACH_TEST_SRC) -o test_beach_generation_sanitize -lm
	./test_beach_generation_sanitize
	$(CC) -Wall -Wextra -O1 -g -std=c99 -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_world_streaming.c world_streaming.c game_logic.c world_generation.c voxel_world.c storage.c -o test_world_streaming_sanitize -lm
	./test_world_streaming_sanitize
	$(CC) -Wall -Wextra -O1 -g -std=c99 -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_game.c game.c game_logic.c world_generation.c voxel_world.c storage.c -o test_game_sanitize -lm
	./test_game_sanitize
	$(CC) -Wall -Wextra -O1 -g -std=c99 -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_storage.c game_logic.c world_generation.c voxel_world.c storage.c -o test_storage_sanitize -lm
	./test_storage_sanitize
	$(CC) -Wall -Wextra -O1 -g -std=c99 -fsanitize=address,undefined -fno-omit-frame-pointer $(TEST_SRC) -o test_game_logic_sanitize -lm
	./test_game_logic_sanitize
	$(CC) -Wall -Wextra -O1 -g -std=c99 -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_allocation_failures.c game_logic.c world_generation.c voxel_world.c storage.c -o test_allocation_failures_sanitize -lm
	./test_allocation_failures_sanitize
	$(CC) -Wall -Wextra -O1 -g -std=c99 -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_pause_menu.c application.c $(COMMON_SRC) $(FIXTURE_FLAGS) $(PAUSE_WRAP) -o test_pause_menu_sanitize $(LDFLAGS)
	./test_pause_menu_sanitize

clean:
	rm -f distant_terrain_capture
	rm -f test_beach_generation test_beach_generation_sanitize
	rm -f test_world_streaming test_world_streaming_sanitize test_game test_game_sanitize local_vertex_capture test_storage test_storage_sanitize allocation_capture section_storage_benchmark mesh_query_benchmark mesh_query_profile streaming_capture terrain_capture visibility_capture performance_capture $(TARGET) $(TEST_TARGET) test_allocation_failures test_renderer test_pause_menu test_game_logic_sanitize test_allocation_failures_sanitize test_pause_menu_sanitize

run: all
	./$(TARGET)

.PHONY: all clean run test test-renderer test-sanitize

performance_capture: tests/performance_capture.c $(SRC) $(APP_HEADERS)
	$(CC) $(CFLAGS) tests/performance_capture.c application.c $(COMMON_SRC) -o $@ $(LDFLAGS)

streaming_capture: tests/streaming_capture.c $(SRC) $(APP_HEADERS) tests/renderer_test.h
	$(CC) $(CFLAGS) tests/streaming_capture.c application.c $(COMMON_SRC) $(FIXTURE_FLAGS) -o $@ $(LDFLAGS)

visibility_capture: tests/visibility_capture.c $(SRC) $(APP_HEADERS) tests/renderer_test.h
	$(CC) $(CFLAGS) tests/visibility_capture.c application.c $(COMMON_SRC) $(FIXTURE_FLAGS) -Wl,--wrap=glDrawArrays -o $@ $(LDFLAGS)

terrain_capture: tests/terrain_capture.c $(SRC) $(APP_HEADERS)
	$(CC) $(CFLAGS) tests/terrain_capture.c application.c $(COMMON_SRC) -o $@ $(LDFLAGS)

distant_terrain_capture: tests/distant_terrain_capture.c $(SRC) $(APP_HEADERS)
	$(CC) $(CFLAGS) tests/distant_terrain_capture.c application.c $(COMMON_SRC) -o $@ $(LDFLAGS)

# Headless CPU benchmark; profile timings include instrumentation overhead.
mesh_query_benchmark: tests/mesh_query_benchmark.c tests/mesh_query_baseline.inc voxel_world.c game_logic.c world_generation.c game_logic.h world_generation.h block_type.h storage.h storage.c
	$(CC) $(CFLAGS) tests/mesh_query_benchmark.c game_logic.c world_generation.c storage.c -o $@ -lm

mesh_query_profile: tests/mesh_query_benchmark.c tests/mesh_query_baseline.inc voxel_world.c game_logic.c world_generation.c game_logic.h world_generation.h block_type.h storage.h storage.c
	$(CC) $(CFLAGS) -DMESH_QUERY_PROFILE tests/mesh_query_benchmark.c game_logic.c world_generation.c storage.c -o $@ -lm

# Requested and retained pooled section storage; independent of allocator internals.
section_storage_benchmark: tests/section_storage_benchmark.c game_logic.c world_generation.c voxel_world.c game_logic.h world_generation.h block_type.h storage.h storage.c
	$(CC) $(CFLAGS) tests/section_storage_benchmark.c game_logic.c world_generation.c voxel_world.c storage.c -o $@ -lm

allocation_capture: tests/allocation_capture.c $(SRC) $(APP_HEADERS) tests/renderer_test.h
	$(CC) $(CFLAGS) tests/allocation_capture.c application.c $(COMMON_SRC) $(FIXTURE_FLAGS) -o $@ $(LDFLAGS)

test_storage: tests/test_storage.c game_logic.c world_generation.c voxel_world.c storage.c storage.h game_logic.h world_generation.h block_type.h
	$(CC) $(CFLAGS) tests/test_storage.c game_logic.c world_generation.c voxel_world.c storage.c -o $@ -lm

# Opt-in real-GL representation/precision comparison; run serially.
local_vertex_capture: tests/local_vertex_capture.c $(SRC) $(APP_HEADERS) tests/renderer_test.h
	$(CC) $(CFLAGS) tests/local_vertex_capture.c application.c $(COMMON_SRC) $(FIXTURE_FLAGS) -o $@ $(LDFLAGS)

test_game: tests/test_game.c game.c game.h game_logic.c world_generation.c voxel_world.c storage.c game_logic.h world_generation.h block_type.h storage.h view.h
	$(CC) $(CFLAGS) tests/test_game.c game.c game_logic.c world_generation.c voxel_world.c storage.c -o $@ -lm

# CPU residency and coordinated work scheduling require no GL or GLFW linkage.
test_world_streaming: tests/test_world_streaming.c world_streaming.c world_streaming.h game_logic.c world_generation.c voxel_world.c game_logic.h world_generation.h block_type.h storage.c storage.h performance.h
	$(CC) $(CFLAGS) tests/test_world_streaming.c world_streaming.c game_logic.c world_generation.c voxel_world.c storage.c -o $@ -lm
