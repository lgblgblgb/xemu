#!/usr/bin/env bash

MYCC="`pwd`/build-scripts/clang-fat.sh"
MYCXX="`pwd`/build-scripts/clang++-fat.sh"
PREFIX="/usr/local/sdl2-xemu"

mkdir -p build || exit 1
cd build || exit 1

ls -l "$MYCC" "$MYCXX" || exit 1

CC="$MYCC" CXX="$MYCXX" ../configure --prefix="$PREFIX" || exit 1

make - || exit 1

sudo make install || exit 1

RESULT="`pwd`/SDL2-xemu-mac-uniflat-`$PREFIX/bin/sdl2-config --version`.tar.gz"

echo $RESULT

cd / || exit 1

sudo tar cfvz "$RESULT" "$PREFIX" || exit 1

ls -l $RESULT

exit 0
