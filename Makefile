CC=gcc
CFLAGS=-Ofast -march=native -pipe -Wall -Wextra -Wnull-dereference \
	-Wmisleading-indentation -Wlogical-op -Wno-unused-parameter

build/png_percept_down: png_percept_down.c
	mkdir -p build
	$(CC) $(CFLAGS) png_percept_down.c -DTILEABLE=0 -o $@ -lpng -lm

all: build/png_percept_down

install:
	mkdir -p $(DESTDIR)/bin
	cp build/png_percept_down $(DESTDIR)/bin/png_percept_down
