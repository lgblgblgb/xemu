#!/bin/bash
# (C)2021 Gabor Lenart LGB, lgblgblgb@gmail.com

FILEREPOBASE="https://github.com/lgblgblgb/xemu/raw/gh-pages/files"

CREATE_DMG_VER="1.0.9"
CREATE_DMG_ARCH="macos-$CREATE_DMG_VER-create-dmg.tar.gz"
CREATE_DMG_DIR="create-dmg/$CREATE_DMG_VER"

SDL2_ARCH="macos-2.0.16-sdl2.tar.gz"

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

cd /usr/local/lgb || exit 1
ls -la $SDL2_ARCH
mkdir sdl2 || exit 1
cd sdl2 || exit 1
echo "Current directory: `pwd`"
tar xfz ../$SDL2_ARCH || exit 1
ls -la
file="`find . -name sdl2-config`"
if [ "$file" == "" ]; then
	echo "ERROR: cannot found sdl2-config until unpacking $SDL2_ARCH" >&2
	exit 1
fi
dir="`dirname $file`"
if [ ! -d $dir ]; then
	echo "ERROR: extracted path $dir from $file does not exist as directory" >&2
	exit 1
fi
cd $dir || exit 1
echo "Current directory: `pwd`"
ls -la
cd .. || exit 1
SDLROOT="`pwd`"
echo "Current directory (should be the SDL2 install root now): $SDLROOT"
ls -la

cd bin || exit 1
echo "Current directory: `pwd`"
ls -la

chmod 755 sdl2-config || exit 1
#./sdl2-config --version --prefix --cflags --libs --static-libs

sed "s#@@HOMEBREW_PREFIX@@#$SDLROOT#g" < sdl2-config > sdl2-config.new || exit 1
cat sdl2-config.new > sdl2-config || exit 1
rm -f sdl2-config.new

echo "Symlinking from `pwd`/sdl2-config to /usr/local/bin/sdl2-config ..."
sudo ln -s "`pwd`/sdl2-config" /usr/local/bin/sdl2-config || exit 1
ls -la "`pwd`/sdl2-config" /usr/local/bin/sdl2-config

cd ../lib || exit 1
echo "Current directory: `pwd`"
for lib in *.dylib ; do
	if [ -L $lib ]; then
		echo "Skip mangling symbolic linked dylib: $lib"
	else
		path="`pwd`/$lib"
		echo "Mangling dylib $lib to $path ..."
		chmod 644 $lib || exit 1
		ls -l $lib
		install_name_tool -id "$path" $lib || exit 1
		ls -l $lib
	fi
done

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
echo "Renaming/moving directory $dir as /usr/local/lgb/create-dmg ..."
mv "$dir" /usr/local/lgb/create-dmg || exit
echo "Directory listing of /usr/local/lgb/create-dmg:"
ls -l /usr/local/lgb/create-dmg/

echo "*** Chown'ing directory /usr/local/lgb to root:wheel ..."
# It seems on Mac, system binaries/etc should have user root AND group wheel ...

sudo chown -R root:wheel /usr/local/lgb || exit 1
ls -lad /usr/local/lgb

echo "**** Chdir from `pwd` to $ORIGCWD ..."
cd "$ORIGCWD" || exit 1

echo "*** $0 is finished successfully"

exit 0
