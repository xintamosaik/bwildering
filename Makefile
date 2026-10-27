CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
LDFLAGS = -lGLEW -lglfw -lGL -lm -lpthread

TARGET = bwildering
SRC = main.c game_logic.c voxel_world.c storage.c performance.c ui.c input.c
TEST_TARGET = test_game_logic
TEST_SRC = tests/test_game_logic.c game_logic.c voxel_world.c storage.c

all: $(TARGET)

$(TARGET): $(SRC) storage.h game_logic.h performance.h application.h ui.h input.h
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

$(TEST_TARGET): $(TEST_SRC) game_logic.h storage.h storage.c
	$(CC) $(CFLAGS) $(TEST_SRC) -o $(TEST_TARGET) -lm

test_allocation_failures: tests/test_allocation_failures.c game_logic.c voxel_world.c game_logic.h storage.h storage.c
	$(CC) $(CFLAGS) tests/test_allocation_failures.c game_logic.c voxel_world.c storage.c -o $@ -lm

test_renderer: tests/test_renderer.c $(SRC) storage.h game_logic.h performance.h application.h ui.h input.h
	$(CC) $(CFLAGS) tests/test_renderer.c game_logic.c voxel_world.c storage.c performance.c ui.c input.c -o $@ $(LDFLAGS)

test_pause_menu: tests/test_pause_menu.c $(SRC) storage.h game_logic.h performance.h application.h ui.h input.h
	$(CC) $(CFLAGS) tests/test_pause_menu.c game_logic.c voxel_world.c storage.c performance.c -o $@ $(LDFLAGS)

test: $(TEST_TARGET) test_allocation_failures test_pause_menu test_storage
	./$(TEST_TARGET)
	./test_allocation_failures
	./test_pause_menu
	./test_storage

# Requires a local X11 display and an OpenGL 3.3 context; uses an invisible window.
test-renderer: test_renderer
	./test_renderer

test-sanitize:
	$(CC) -Wall -Wextra -O1 -g -std=c99 -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_storage.c game_logic.c voxel_world.c storage.c -o test_storage_sanitize -lm
	./test_storage_sanitize
	$(CC) -Wall -Wextra -O1 -g -std=c99 -fsanitize=address,undefined -fno-omit-frame-pointer $(TEST_SRC) -o test_game_logic_sanitize -lm
	./test_game_logic_sanitize
	$(CC) -Wall -Wextra -O1 -g -std=c99 -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_allocation_failures.c game_logic.c voxel_world.c storage.c -o test_allocation_failures_sanitize -lm
	./test_allocation_failures_sanitize
	$(CC) -Wall -Wextra -O1 -g -std=c99 -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_pause_menu.c game_logic.c voxel_world.c storage.c performance.c -o test_pause_menu_sanitize $(LDFLAGS)
	./test_pause_menu_sanitize

clean:
	rm -f test_storage test_storage_sanitize allocation_capture section_storage_benchmark mesh_query_benchmark mesh_query_profile streaming_capture terrain_capture visibility_capture performance_capture $(TARGET) $(TEST_TARGET) test_allocation_failures test_renderer test_pause_menu test_game_logic_sanitize test_allocation_failures_sanitize test_pause_menu_sanitize

run: all
	./$(TARGET)

.PHONY: all clean run test test-renderer test-sanitize

performance_capture: tests/performance_capture.c $(SRC) storage.h game_logic.h performance.h application.h ui.h input.h
	$(CC) $(CFLAGS) tests/performance_capture.c game_logic.c voxel_world.c storage.c performance.c ui.c input.c -o $@ $(LDFLAGS)

streaming_capture: tests/streaming_capture.c $(SRC) storage.h game_logic.h performance.h application.h ui.h input.h
	$(CC) $(CFLAGS) tests/streaming_capture.c game_logic.c voxel_world.c storage.c performance.c ui.c input.c -o $@ $(LDFLAGS)

visibility_capture: tests/visibility_capture.c $(SRC) storage.h game_logic.h performance.h application.h ui.h input.h
	$(CC) $(CFLAGS) tests/visibility_capture.c game_logic.c voxel_world.c storage.c performance.c ui.c input.c -o $@ $(LDFLAGS)

terrain_capture: tests/terrain_capture.c $(SRC) storage.h game_logic.h performance.h application.h ui.h input.h
	$(CC) $(CFLAGS) tests/terrain_capture.c game_logic.c voxel_world.c storage.c performance.c ui.c input.c -o $@ $(LDFLAGS)

# Headless CPU benchmark; profile timings include instrumentation overhead.
mesh_query_benchmark: tests/mesh_query_benchmark.c tests/mesh_query_baseline.inc voxel_world.c game_logic.c game_logic.h storage.h storage.c
	$(CC) $(CFLAGS) tests/mesh_query_benchmark.c game_logic.c storage.c -o $@ -lm

mesh_query_profile: tests/mesh_query_benchmark.c tests/mesh_query_baseline.inc voxel_world.c game_logic.c game_logic.h storage.h storage.c
	$(CC) $(CFLAGS) -DMESH_QUERY_PROFILE tests/mesh_query_benchmark.c game_logic.c storage.c -o $@ -lm

# Requested and retained pooled section storage; independent of allocator internals.
section_storage_benchmark: tests/section_storage_benchmark.c game_logic.c voxel_world.c game_logic.h storage.h storage.c
	$(CC) $(CFLAGS) tests/section_storage_benchmark.c game_logic.c voxel_world.c storage.c -o $@ -lm

allocation_capture: tests/allocation_capture.c $(SRC) storage.h game_logic.h application.h
	$(CC) $(CFLAGS) tests/allocation_capture.c game_logic.c voxel_world.c storage.c performance.c ui.c input.c -o $@ $(LDFLAGS)

test_storage: tests/test_storage.c game_logic.c voxel_world.c storage.c storage.h game_logic.h
	$(CC) $(CFLAGS) tests/test_storage.c game_logic.c voxel_world.c storage.c -o $@ -lm
