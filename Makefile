CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -g
TARGET  = fs_simulator
SRC     = src/main.c src/utils.c src/fs.c src/disco.c src/raid.c src/escalonador.c src/ssd.c src/es.c
OBJ     = $(SRC:.c=.o)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ): src/utils.h src/disco.h src/fs.h src/raid.h src/escalonador.h src/ssd.h src/es.h

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: clean