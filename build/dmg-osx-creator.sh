#!/bin/bash
# (C)2020 Gabor Lenart LGB, lgblgblgb@gmail.com
# -------------------------------------------------
# Requires create-dmg (eg with homebrew'ing it ...)
# https://github.com/andreyvit/create-dmg
# -------------------------------------------------

BUNDLE="yes"
#TIMESTAMP="`date '+%Y%m%d%H%M%S'`"
TIMESTAMP="`cat build/objs/cdate.data`"

echo "*** *** DMG begin: $0 @ `date` *** ***"

# Determine MacOS version
# Should give us some numeric answer like: 10.15.1
# We can use this info to decide on some options ...
MACOSVER="`sw_vers -productVersion`"
echo "MacOS version number: $MACOSVER"
if [ "$MACOSVER" != "" ]; then
	MACOSMAJORVER="`echo $MACOSVER | cut -f1 -d.`"
	MACOSMINORVER="`echo $MACOSVER | cut -f2 -d.`"
	MACOSPATCHVER="`echo $MACOSVER | cut -f3 -d.`"
	echo "MacOS major version: $MACOSMAJORVER"
	echo "MacOS minor version: $MACOSMINORVER"
	echo "MacOS patch version: $MACOSPATCHVER"
	if [ "$MACOSMAJORVER" = "10" -a "$MACOSMINORVER" -ge "15" ]; then
		echo "Post-10.15 version is detected."
	else
		echo "Pre-10.15 version is detected."
	fi
fi

# Though on travis we can know branch from $BRANCH_TRAVIS
# that would be problematic when run this script on other
# environments. So let's figure out the branch as at own!
# Also the last commit ...
if [ "$TRAVIS_BRANCH" = "" ]; then
	BRANCH="`git rev-parse --symbolic-full-name --abbrev-ref HEAD`"
	#BRANCH="`git branch | sed -n 's/^\*[\t ]*//p'`"
else
	BRANCH="$TRAVIS_BRANCH"
fi
if [ "$TRAVIS_COMMIT" = "" ]; then
	COMMIT="`git rev-parse HEAD`"
else
	COMMIT="$TRAVIS_COMMIT"
fi

mkdir .dmg .dmg/bin || exit 1

if [ "$BUNDLE" = "yes" ]; then
	dylink=""
	for a in build/bin/*.native ; do
		b="`basename $a .native`"
		mkdir -p .dmg/$b.app/Contents/{Frameworks,MacOS,Resources}
		cp $a .dmg/$b.app/Contents/MacOS/$b
		if [ "$dylink" = "" ]; then
			dylink=".dmg/$b.app/Contents/Frameworks/libSDL2-xemu.dylib"
			cp build/bin/libSDL2-xemu.dylib $dylink
		else
			ln $dylink .dmg/$b.app/Contents/Frameworks/libSDL2-xemu.dylib
		fi
		install_name_tool -change @executable_path/libSDL2-xemu.dylib @executable_path/../Frameworks/libSDL2-xemu.dylib .dmg/$b.app/Contents/MacOS/$b
		ln -s ../$b.app/Contents/MacOS/$b .dmg/bin/$b
		cp build/xemu.icns .dmg/$b.app/Contents/Resources/$b.icns
		echo "<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">
<plist version=\"1.0\">
<dict>
	<key>CFBundleExecutable</key>
	<string>$b</string>
	<key>CFBundleGetInfoString</key>
	<string>$b $BRANCH $TIMESTAMP</string>
	<key>CFBundleIconFile</key>
	<string>$b.icns</string>
	<key>CFBundleIdentifier</key>
	<string>org.lgb.xemu.$b</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>$TIMESTAMP-$BRANCH</string>
	<key>CFBundleName</key>
	<string>$b</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleSignature</key>
	<string>XEMU</string>
	<key>CFBundleVersion</key>
	<string>$TIMESTAMP-$BRANCH</string>
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
	for a in build/bin/*.native ; do
		b=".dmg/`basename $a .native`"
		echo "Copying OSX binary $a -> $b"
		cp -a $a $b
	done
fi

cp README.md .dmg/README-XEMU.md
cp LICENSE .dmg/
echo "For generic information on Xemu please read: README-XEMU.md
For version informations on this very DMG: THIS_VERSION.txt
For license/copyright information: LICENSE

This file is about the DMG distribution media of Xemu.

Generally you may want to start the '.app' items via Finder.
For command line use, I would recommend to use binaries in the bin/
folder, probably from your own project and/or starting from terminal.
They're symlinks for real." > .dmg/README.txt

echo "Buildsys: MacOS $MACOSVER `whoami`@`hostname` uname: `uname -a`
Branch: $BRANCH
Commit: $COMMIT
Link:   https://github.com/lgblgblgb/xemu/commit/$COMMIT
Remote: `git config --get remote.origin.url`" > .dmg/THIS_VERSION.txt
if [ "$TRAVIS_REPO_SLUG" != "" -a "$TRAVIS_COMMIT" != "" ]; then
        echo "Commit URL: https://github.com/$TRAVIS_REPO_SLUG/commit/$TRAVIS_COMMIT" >> .dmg/THIS_VERSION.txt
fi
echo >> .dmg/THIS_VERSION.txt

cat .dmg/THIS_VERSION.txt

env | grep '^TRAVIS_' | grep -v '=$' | grep -vi secure | sort | sed 's/=/ = /' >> .dmg/THIS_VERSION.txt
git log -25 >> .dmg/THIS_VERSION.txt

echo "*** DMG content will be:"

find .dmg -ls

echo "*** Starting create-dmg now ***"

# Note: I have no idea on mac .. what is "add-drop-link" what application needs to be dropped etc? Why it is needed at all?

time create-dmg	\
	--volname "Xemu - $BRANCH - $TIMESTAMP" \
	--volicon "build/xemu.icns" \
	--eula LICENSE \
	--background "build/xemu-bg.png" \
	--window-pos 200 120 \
	--window-size 800 400 \
	--icon-size 100 \
	--icon "Application.app" 200 190 \
	--hide-extension "Application.app" \
	--app-drop-link 600 185 \
	Xemu-Installer.dmg	\
	.dmg

ls -l Xemu-Installer.dmg

echo "*** *** DMG end: $0 @ `date` *** ***"

exit 0
