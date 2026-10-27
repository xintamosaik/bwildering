CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
LDFLAGS = -lGLEW -lglfw -lGL -lm -lpthread

TARGET = bwildering
SRC = main.c game_logic.c voxel_world.c performance.c ui.c
TEST_TARGET = test_game_logic
TEST_SRC = tests/test_game_logic.c game_logic.c voxel_world.c

all: $(TARGET)

$(TARGET): $(SRC) game_logic.h performance.h application.h ui.h
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

$(TEST_TARGET): $(TEST_SRC) game_logic.h
	$(CC) $(CFLAGS) $(TEST_SRC) -o $(TEST_TARGET) -lm

test_allocation_failures: tests/test_allocation_failures.c game_logic.c voxel_world.c game_logic.h
	$(CC) $(CFLAGS) tests/test_allocation_failures.c game_logic.c voxel_world.c -o $@ -lm -Wl,--wrap=malloc,--wrap=calloc

test_renderer: tests/test_renderer.c $(SRC) game_logic.h performance.h application.h ui.h
	$(CC) $(CFLAGS) tests/test_renderer.c game_logic.c voxel_world.c performance.c ui.c -o $@ $(LDFLAGS)

test_pause_menu: tests/test_pause_menu.c $(SRC) game_logic.h performance.h application.h ui.h
	$(CC) $(CFLAGS) tests/test_pause_menu.c game_logic.c voxel_world.c performance.c -o $@ $(LDFLAGS)

test: $(TEST_TARGET) test_allocation_failures test_pause_menu
	./$(TEST_TARGET)
	./test_allocation_failures
	./test_pause_menu

# Requires a local X11 display and an OpenGL 3.3 context; uses an invisible window.
test-renderer: test_renderer
	./test_renderer

test-sanitize:
	$(CC) -Wall -Wextra -O1 -g -std=c99 -fsanitize=address,undefined -fno-omit-frame-pointer $(TEST_SRC) -o test_game_logic_sanitize -lm
	./test_game_logic_sanitize
	$(CC) -Wall -Wextra -O1 -g -std=c99 -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_allocation_failures.c game_logic.c voxel_world.c -o test_allocation_failures_sanitize -lm -Wl,--wrap=malloc,--wrap=calloc
	./test_allocation_failures_sanitize
	$(CC) -Wall -Wextra -O1 -g -std=c99 -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_pause_menu.c game_logic.c voxel_world.c performance.c -o test_pause_menu_sanitize $(LDFLAGS)
	./test_pause_menu_sanitize

clean:
	rm -f section_storage_benchmark mesh_query_benchmark mesh_query_profile streaming_capture terrain_capture visibility_capture performance_capture $(TARGET) $(TEST_TARGET) test_allocation_failures test_renderer test_pause_menu test_game_logic_sanitize test_allocation_failures_sanitize test_pause_menu_sanitize

run: all
	./$(TARGET)

.PHONY: all clean run test test-renderer test-sanitize

performance_capture: tests/performance_capture.c $(SRC) game_logic.h performance.h application.h ui.h
	$(CC) $(CFLAGS) tests/performance_capture.c game_logic.c voxel_world.c performance.c ui.c -o $@ $(LDFLAGS)

streaming_capture: tests/streaming_capture.c $(SRC) game_logic.h performance.h application.h ui.h
	$(CC) $(CFLAGS) tests/streaming_capture.c game_logic.c voxel_world.c performance.c ui.c -o $@ $(LDFLAGS)

visibility_capture: tests/visibility_capture.c $(SRC) game_logic.h performance.h application.h ui.h
	$(CC) $(CFLAGS) tests/visibility_capture.c game_logic.c voxel_world.c performance.c ui.c -o $@ $(LDFLAGS)

terrain_capture: tests/terrain_capture.c $(SRC) game_logic.h performance.h application.h ui.h
	$(CC) $(CFLAGS) tests/terrain_capture.c game_logic.c voxel_world.c performance.c ui.c -o $@ $(LDFLAGS)

# Headless CPU benchmark; profile timings include instrumentation overhead.
mesh_query_benchmark: tests/mesh_query_benchmark.c tests/mesh_query_baseline.inc voxel_world.c game_logic.c game_logic.h
	$(CC) $(CFLAGS) tests/mesh_query_benchmark.c game_logic.c -o $@ -lm

mesh_query_profile: tests/mesh_query_benchmark.c tests/mesh_query_baseline.inc voxel_world.c game_logic.c game_logic.h
	$(CC) $(CFLAGS) -DMESH_QUERY_PROFILE tests/mesh_query_benchmark.c game_logic.c -o $@ -lm

# Linux/glibc allocator-inclusive resident section accounting.
section_storage_benchmark: tests/section_storage_benchmark.c game_logic.c voxel_world.c game_logic.h
	$(CC) $(CFLAGS) tests/section_storage_benchmark.c game_logic.c voxel_world.c -o $@ -lm
