CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
LDFLAGS = -lGLEW -lglfw -lGL -lm -lpthread

TARGET = bwildering
SRC = main.c game_logic.c
TEST_TARGET = test_game_logic
TEST_SRC = tests/test_game_logic.c game_logic.c

all: $(TARGET)

$(TARGET): $(SRC) game_logic.h
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

$(TEST_TARGET): $(TEST_SRC) game_logic.h
	$(CC) $(CFLAGS) $(TEST_SRC) -o $(TEST_TARGET) -lm

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -f $(TARGET) $(TEST_TARGET)

run: all
	./$(TARGET)

.PHONY: all clean run test
