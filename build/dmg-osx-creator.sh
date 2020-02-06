#!/bin/bash
# Requires create-dmg (eg with homebrew'ing it ...)
# https://github.com/andreyvit/create-dmg

echo "DMG begin"

mkdir .dmg

cp * .dmg/ || true

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
