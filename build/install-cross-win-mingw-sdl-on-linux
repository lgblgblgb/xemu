#!/bin/bash
# (C)2020 LGB Gabor Lenart <lgblgblgb@gmail.com>

set -e

if [ "$1" = "" ]; then
	echo "ERROR: bad usage, missing argument" >&2
	exit 1
fi

#SDL_WIN_URL="https://github.com/lgblgblgb/xep128/raw/gh-pages/files/sdl204-win-crosstools.tar.bz2"
SDL_WIN_URL="https://github.com/lgblgblgb/xemu/raw/gh-pages/files/sdl-2.0.10-win-mingw-crosstools-on-linux-installed.tar.bz2"

# DO NOT modify CROSS_ROOT! It won't work, as it depends on the download tarball how it was that installed!!!
CROSS_ROOT="/usr/local/cross-tools"

BIN_DIR="$1"

if [ "`whoami`" != "root" ]; then
	echo "WARNING: running $0 as non-root user, defaulting to use 'sudo' for certain tasks."
	echo "         This will cause problems if you are not allowed to 'sudo'."
	SUDO_CMD="sudo"
else
	SUDO_CMD=""
fi

error=0
if [ -d $CROSS_ROOT/i686-w64-mingw32 ]; then
	echo "ERROR: Directory $CROSS_ROOT/i686-w64-mingw32 already exists" >&2
	echo "       Please remove this directory if you really want to reinstall sdl-cross-win-mingw-xemu" >&2
	error=1
fi
if [ -d $CROSS_ROOT/x86_64-w64-mingw32 ]; then
	echo "ERROR: Directory $CROSS_ROOT/x86_64-w64-mingw32 already exists" >&2
	echo "       Please remove this directory if you really want to reinstall sdl-cross-win-mingw-xemu" >&2
	error=1
fi
if [ -f $BIN_DIR/i686-w64-mingw32-sdl2-config ]; then
	echo "ERROR: File $BIN_DIR/i686-w64-mingw32-sdl2-config already exists" >&2
	echo "       Please remove this file if you really want to reinstall sdl-cross-win-mingw-xemu" >&2
	error=1
fi
if [ -f $BIN_DIR/x86_64-w64-mingw32-sdl2-config ]; then
	echo "ERROR: File $BIN_DIR/x86_64-w64-mingw32-sdl2-config already exists" >&2
	echo "       Please remove this file if you really want to reinstall sdl-cross-win-mingw-xemu" >&2
	error=1
fi
if [ "$error" != "0" ]; then
	echo "ERROR: *** Aborting now, because of failed pre-check(s) ***" >&2
	exit 1
fi

if [ ! -d $CROSS_ROOT ]; then
	echo "*** Creating directory $CROSS_ROOT"
	$SUDO_CMD mkdir -p $CROSS_ROOT
fi
if [ ! -d $BIN_DIR ]; then
	echo "*** Creating directory $BIN_DIR"
	$SUDO_CMD mkdir -p $BIN_DIR
fi
PATH="$BIN_DIR:$PATH"
export PATH

echo "*** Downloading from $SDL_WIN_URL"
rm -f /tmp/sdl-win.tar.bz2
wget -q -O /tmp/sdl-win.tar.bz2 --no-check-certificate $SDL_WIN_URL
ls -l /tmp/sdl-win.tar.bz2

echo "*** Extracting archive"
$SUDO_CMD tar xfvj /tmp/sdl-win.tar.bz2 -C $CROSS_ROOT
find $CROSS_ROOT -ls
rm -f /tmp/sdl-win.tar.bz2

echo "*** Creating win32 sdl2-config symlink and test it: $BIN_DIR/i686-w64-mingw32-sdl2-config"
$SUDO_CMD ln -s $CROSS_ROOT/i686-w64-mingw32/bin/sdl2-config $BIN_DIR/i686-w64-mingw32-sdl2-config
i686-w64-mingw32-sdl2-config --version --prefix --cflags --libs --static-libs

echo "*** Creating win64 sdl2-config symlink and test it: $BIN_DIR/x86_64-w64-mingw32-sdl2-config"
$SUDO_CMD ln -s $CROSS_ROOT/x86_64-w64-mingw32/bin/sdl2-config $BIN_DIR/x86_64-w64-mingw32-sdl2-config
x86_64-w64-mingw32-sdl2-config --version --prefix --cflags --libs --static-libs
ls -l $BIN_DIR/i686-w64-mingw32-sdl2-config $BIN_DIR/x86_64-w64-mingw32-sdl2-config

set +e

echo "Everything seems to be OK :)"
exit 0
