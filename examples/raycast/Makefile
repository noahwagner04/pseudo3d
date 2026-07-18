# examples/raycast/Makefile
CC     ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Wpedantic -O2
INC     = -I../..

all: sdl

sdl: sdl.c ../../p3d_raycast.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(INC) $$(pkg-config --cflags sdl3) sdl.c $$(pkg-config --libs sdl3) -lm -o build/sdl

raylib: raylib.c ../../p3d_raycast.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(INC) $$(pkg-config --cflags raylib) raylib.c $$(pkg-config --libs raylib) -lm -o build/raylib

terminal: terminal.c ../../p3d_raycast.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(INC) terminal.c -lm -o build/terminal

clean:
	rm -rf build

.PHONY: all sdl raylib terminal clean
