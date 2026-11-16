#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
# Build tools/routes_host.cpp with the port's game logic for the host, then
#   tools/routes.sh                    play tools/routes.json through all six caverns
#   tools/routes.sh search N [BEAM]    search a route through cavern N (1-6)
set -e
cd "$(dirname "$0")/.."
mkdir -p build
CC=${CC:-cc}
CXX=${CXX:-c++}
for f in game plat tmap levels levels_data; do
    $CC -O2 -w -D__fastcall__= -Isrc -c -o build/host_$f.o src/$f.c
done
$CXX -O2 -std=c++11 -w -D__fastcall__= -Isrc -o build/routes_host tools/routes_host.cpp \
    build/host_game.o build/host_plat.o build/host_tmap.o build/host_levels.o build/host_levels_data.o
if [ "$1" = search ]; then shift; exec build/routes_host search "$@"; fi
exec build/routes_host check tools/routes.json
