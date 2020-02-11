#!/bin/bash
# (C)2020 Gabor Lenart LGB, lgblgblgb@gmail.com
# -------------------------------------------------
# Requires create-dmg (eg with homebrew'ing it ...)
# https://github.com/andreyvit/create-dmg
# -------------------------------------------------

BUNDLE="yes"
TIMESTAMP="`date '+%Y%m%d%H%M%S'`"

echo "DMG begin"

mkdir .dmg

if [ "$BUNDLE" = "yes" ]; then
	dylink=""
	for a in build/bin/*.osx ; do
		b="`basename $a .osx`"
		mkdir -p .dmg/$b.app/Contents/{Frameworks,MacOS,Resources}
		cp $a .dmg/$b.app/Contents/MacOS/$b
		if [ "$dylink" = "" ]; then
			dylink=".dmg/$b.app/Contents/Frameworks/libSDL2-xemu.dylib"
			cp build/bin/libSDL2-xemu.dylib $dylink
		else
			ln $dylink .dmg/$b.app/Contents/Frameworks/libSDL2-xemu.dylib
		fi
		install_name_tool -change @executable_path/libSDL2-xemu.dylib @executable_path/../Frameworks/libSDL2-xemu.dylib .dmg/$b.app/Contents/MacOS/$b
		cp build/xemu.icns .dmg/$b.app/Contents/Resources/$b.icns
		echo "<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">
<plist version=\"1.0\">
<dict>
	<key>CFBundleExecutable</key>
	<string>$b</string>
	<key>CFBundleGetInfoString</key>
	<string>$b $TRAVIS_BRANCH $TIMESTAMP</string>
	<key>CFBundleIconFile</key>
	<string>$b.icns</string>
	<key>CFBundleIdentifier</key>
	<string>org.lgb.xemu.$b</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>$TIMESTAMP-$TRAVIS_BRANCH</string>
	<key>CFBundleName</key>
	<string>$b</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleSignature</key>
	<string>XEMU</string>
	<key>CFBundleVersion</key>
	<string>$TIMESTAMP-$TRAVIS_BRANCH</string>
	<key>NSHighResolutionCapable</key>
	<true/>
	<key>NSAppSleepDisabled</key>
	<true/>
	<key>LSApplicationCategoryType</key>
	<string>public.app-category.education</string>
</dict>
</plist>" > .dmg/$b.app/Contents/Info.plist
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

time create-dmg	\
	--volname "Xemu - $TRAVIS_BRANCH - $TIMESTAMP" \
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
