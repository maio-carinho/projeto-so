CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -g -Isrc
TARGET  = fs_simulator
SRC     = $(wildcard src/*.c src/*/*.c)
OBJ     = $(SRC:.c=.o)
HEADERS = $(wildcard src/*/*.h)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ): $(HEADERS)

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: clean