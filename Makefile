CC := gcc
CFLAGS := -std=c99 -Wall -Wextra -O2

all: quash

quash: shell.c
	$(CC) $(CFLAGS) $< -o $@

run: quash
	./quash

clean:
	rm -f quash *.o
