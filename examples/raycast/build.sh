#!/bin/sh
CFLAGS="-O2 -std=c99 -Wall -Wextra -I../.. $(pkg-config --cflags sdl3)"
LIBS="$(pkg-config --libs sdl3) -lm"
mkdir -p build
cc $CFLAGS main.c $LIBS -o build/a.out
