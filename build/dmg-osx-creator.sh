#!/bin/bash
# Requires create-dmg (eg with homebrew'ing it ...)
# https://github.com/andreyvit/create-dmg

echo "DMG begin"

mkdir .dmg

for a in build/bin/*.osx ; do
	b=".dmg/`basename $a .osx`"
	echo "Copying OSX binary $a -> $b"
	cp -a $a $b
done

cp build/bin/*.dylib .dmg/
cp README.md LICENSE .dmg/

echo "*** DMG content will be:"

find .dmg -ls

echo "*** Starting create-dmg now ***"

create-dmg	\
	--volname "Xemu Installer" \
	--volicon "build/xemu.icns" \
	--window-pos 200 120 \
	--window-size 800 400 \
	--icon-size 100 \
	--icon "Application.app" 200 190 \
	--hide-extension "Application.app" \
	--app-drop-link 600 185 \
	Xemu-Installer.dmg	\
	.dmg

ls -l Xemu-Installer.dmg

echo "DMG end"
