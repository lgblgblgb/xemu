#!/bin/bash
# (C)2020 Gabor Lenart LGB, lgblgblgb@gmail.com
# -------------------------------------------------
# Requires create-dmg (eg with homebrew'ing it ...)
# https://github.com/andreyvit/create-dmg
# -------------------------------------------------

BUNDLE="no"

echo "DMG begin"

mkdir .dmg

if [ "$BUNDLE" = "yes" ]; then
	for a in build/bin/*.osx ; do
		b="`basename $a .osx`"
		mkdir -p .dmg/$b.app/Contents/{Frameworks,MacOS,Resources}
		cp $a .dmg/$b.app/Contents/MacOS/$b
		cp build/bin/*.dylib .dmg/$b.app/Contents/Frameworks/
	done
else
	cp build/bin/*.dylib .dmg/
	for a in build/bin/*.osx ; do
		b=".dmg/`basename $a .osx`"
		echo "Copying OSX binary $a -> $b"
		cp -a $a $b
	done
fi

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
