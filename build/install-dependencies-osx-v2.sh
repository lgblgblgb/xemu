#!/bin/bash
# (C)2021,2026 Gabor Lenart LGB, lgblgblgb@gmail.com

FILEREPOBASE="https://github.com/lgblgblgb/xemu/raw/gh-pages/files"

CREATE_DMG_VER="1.0.9"
CREATE_DMG_ARCH="macos-$CREATE_DMG_VER-create-dmg.tar.gz"
CREATE_DMG_DIR="create-dmg-$CREATE_DMG_VER"

SDL2_ARCH="macos-SDL2-xemu-uniflat-2.32.10.tar.gz"

echo "*** $0 is running ..."

ORIGCWD="`pwd`"
MYUSER="`whoami`"
echo "Current directory: $ORIGCWD"
echo "Current user: $MYUSER"
echo "Creating directory /usr/local/lgb ..."
sudo mkdir /usr/local/lgb || exit 1
ls -lad /usr/local/lgb
echo "Chown'ing directory /usr/local/lgb to $MYUSER ..."
sudo chown $MYUSER /usr/local/lgb || exit 1
ls -lad /usr/local/lgb

echo "*** Fetching dependencies"

cd /usr/local/lgb || exit 1

for file in $CREATE_DMG_ARCH $SDL2_ARCH ; do
	url="$FILEREPOBASE/$file"
	echo "Fetching: $url"
	wget --no-verbose "$url" || exit 1
	#wget --verbose "$url" || exit 1
	ls -l "`pwd`/$file"
done

echo "*** Installing $SDL2_ARCH"

SDLROOT="/usr/local/sdl2-xemu"
SDL2CONFIG="/usr/local/bin/sdl2-config"

cd / || exit 1
ls -la /usr/local/lgb/$SDL2_ARCH
sudo tar xfz /usr/local/lgb/$SDL2_ARCH || exit 1
sudo chown -R root:wheel "$SDLROOT" || exit 1
ls -la "$SDLROOT" || exit 1

echo "Creating wrapper script to be able to call $SDLROOT/bin/sdl2-config as $SDL2CONFIG ..."
ls -l $SDLROOT/bin/sdl2-config
echo "#!/usr/bin/env bash" | sudo tee $SDL2CONFIG
echo "exec $SDLROOT/bin/sdl2-config \$@" | sudo tee -a $SDL2CONFIG
chmod +x $SDL2CONFIG || exit 1
ls -l $SDL2CONFIG
echo "Testing $SDL2CONFIG:"
$SDL2CONFIG --version --prefix --cflags --libs || exit 1

echo "*** Installing $CREATE_DMG_ARCH"

cd /usr/local/lgb || exit 1
ls -la $CREATE_DMG_ARCH
tar xfz $CREATE_DMG_ARCH || exit 1
rm -f $CREATE_DMG_ARCH || exit 1
dir="/usr/local/lgb/$CREATE_DMG_DIR"
if [ ! -d "$dir" ]; then
	echo "ERROR: directory $dir does not exist" >&2
	exit 1
fi
cd "$dir" || exit 1
pwd
ls -la
echo "Calling sudo make install ..."
sudo make install || exit 1
ls -la /usr/local/bin/create-dmg

echo "*** Chown'ing directory /usr/local/lgb to root:wheel ..."
# It seems on Mac, system binaries/etc should have user root AND group wheel ...

sudo chown -R root:wheel /usr/local/lgb || exit 1
ls -lad /usr/local/lgb

echo "**** Chdir from `pwd` to $ORIGCWD ..."
cd "$ORIGCWD" || exit 1

echo "*** $0 is finished successfully"

exit 0
