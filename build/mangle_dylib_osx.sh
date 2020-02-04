#!/bin/bash
# (C)2016-2020 LGB Gabor Lenart
# https://github.com/lgblgblgb/xemu

set -e

bin="$1"

echo "*** Mangling binary $bin ..."
ls -l $bin
strip $bin
ls -l $bin
otool -L $bin
sdl=`otool -L $bin | awk '$1 ~ /lib[sS][dD][lL]2.*dylib$/ { print $1 }'`
sdl_local="libSDL2-xemu.dylib"
echo "SDL2 library is: $sdl -> $sdl_local"
install_name_tool -change $sdl @executable_path/$sdl_local $bin
strip $bin
ls -l $bin
otool -L $bin
cat $sdl > `dirname $bin`/$sdl_local

exit 0
