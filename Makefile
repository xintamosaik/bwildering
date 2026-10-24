CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
LDFLAGS = -lGLEW -lglfw -lGL -lm -lpthread

TARGET = bwildering
SRC = main.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)

run: all
	./$(TARGET)

.PHONY: all clean run
