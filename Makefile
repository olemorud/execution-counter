
CC = gcc
CFLAGS = -O3 -g -Wall -Wextra -Werror -std=gnu99 -pedantic

all: bin/exec_tracker

clean: bin
	rm -f 'bin/exec_tracker'

bin/exec_tracker: exec_tracker.c bin
	$(CC) -o $@  $(CFLAGS) $<

bin:
	mkdir bin
