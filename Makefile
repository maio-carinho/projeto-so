CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -g
TARGET  = fs_simulator
SRC     = src/main.c src/utils.c src/fs.c src/disco.c
OBJ     = $(SRC:.c=.o)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ): src/utils.h src/disco.h src/fs.h

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: clean