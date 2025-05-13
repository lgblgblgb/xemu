#!/bin/bash
# (C)2025 Gabor Lenart LGB, lgblgblgb@gmail.com

set -e

ANDROID_SDK=${ANDROID_SDK:-"$HOME/android/Sdk"}
MIN_SDK=21
TARGET_SDK=34
PLATFORM=$ANDROID_SDK/platforms/android-$TARGET_SDK/android.jar

XEMU_SO="$1"
if [ "$XEMU_SO" == "" ]; then
	echo "Missing argument: path to the *.android file being a shared libmain.so for real" >&2
	exit 1
fi

cd "`dirname $XEMU_SO`"
cd ../..
PROJECT_ROOT="`pwd`"
XEMU_SO="`basename $XEMU_SO`"

XEMU_TARGET="`echo $XEMU_SO | cut -f1 -d.`"
a="$XEMU_TARGET.android"
if [ "$a" != "$XEMU_SO" ]; then
	echo "Bad call, $a != $XEMU_SO" >&2
	exit 1
fi

if [ ! -x build/configure/sdl2-config-for-android ]; then
	echo "Missing configurator for android" >&2
	exit 1
fi

ARCH="`build/configure/sdl2-config-for-android --sysarch`"
SDL2_SO="`build/configure/sdl2-config-for-android --prefix`/build/android/lib/$ARCH/libSDL2.so"
DEX="`build/configure/sdl2-config-for-android --prefix`/build/classes.dex"

APP_PACKAGE="hu.lgb.xemu.`echo $XEMU_TARGET | cut -c2-`"
APP_NAME="Xemu `echo $XEMU_TARGET | tr '[a-z]' '[A-Z]' | cut -c2-`"
# UNIX timestamp but in minutes. So it won't overflow signed 32-bit limit any time soon (when it will do, I won't have been caring anymore since 4000 years already, or so ...)
VERSION_CODE="$(($(date '+%s') / 60))"
TARGET_DIR="$PROJECT_ROOT/build/bin"
APK_TARGET="$TARGET_DIR/$XEMU_TARGET.apk"
BUILD_DIR="$TARGET_DIR/.apk"
SIGN_DNAME="CN=ApkDebugSigner, O=Xemu, DC=LGB, DC=hu"

VERSION_NAME="$PROJECT_ROOT/build/objs/cdate.data"
if [ -s "$VERSION_NAME" ]; then
	VERSION_NAME="`cat $VERSION_NAME`"
else
	VERSION_NAME="unknown"
fi

KEYSTORE="/etc/local/private/keystore/xemu/keystore.jks"
KEYPASS="/etc/local/private/keystore/xemu/keypass.txt"
STOREPASS="/etc/local/private/keystore/xemu/storepass.txt"

echo "Project root: $PROJECT_ROOT"
echo "Main sh.obj.: $XEMU_SO"
echo "SDL sh.obj. : $SDL2_SO"
echo "Android DEX : $DEX"
echo "App. name   : $APP_NAME"
echo "App. package: $APP_PACKAGE"
echo "Version code: $VERSION_CODE"
echo "Version name: $VERSION_NAME"
echo "Target APK  : $APK_TARGET"
echo "Build dir.  : $BUILD_DIR"
echo "Architecture: $ARCH"
echo "Target SDK  : $TARGET_SDK"
echo "Minimal SDK : $MIN_SDK"

ICON_ORIG="$PROJECT_ROOT/build/xemu-48x48.xpm"

echo "Entering into target binary directory: $TARGET_DIR"
cd "$TARGET_DIR"

for a in "$XEMU_SO" "$SDL2_SO" "$DEX" "$ICON_ORIG" "$PLATFORM" "$KEYPASS" "$STOREPASS" ; do
	echo -n "Checking $a ... "
	if [ ! -s "$a" ]; then
		echo "ERROR"
		echo "$a does not exist (or empty file)" >&2
		exit 1
	fi
	echo "OK [`file $a | cut -d: -f2- | sed 's/^[[:space:]]*//'`]"
done

#echo "Building APK $APP_PACKAGE ($APP_NAME) for $ARCH version $VERSION_CODE SDK $TARGET_SDK (min: $MIN_SDK)"

ASSETS_DIR=$BUILD_DIR/assets
LIB_DIR=$BUILD_DIR/lib/$ARCH
RES_DIR=$BUILD_DIR/res
APK_UNSIGNED=$BUILD_DIR/unsigned.apk
APK_ALIGNED=$BUILD_DIR/aligned.apk
APK_FINAL=$BUILD_DIR/final.apk
MANIFEST=$BUILD_DIR/AndroidManifest.xml

rm -fr "$BUILD_DIR"

mkdir -p "$ASSETS_DIR" "$LIB_DIR" "$RES_DIR"

# Copying files
for a in "$XEMU_SO:$LIB_DIR/libmain.so" "$SDL2_SO:$LIB_DIR/libSDL2.so" "$DEX:$BUILD_DIR/"
do
	s="`echo $a | cut -f1 -d:`"
	d="`echo $a | cut -f2- -d:`"
	echo "Copying: $s -> $d"
	cp -a "$s" "$d"
done
#cp -a "$XEMU_SO" "$LIB_DIR/libmain.so"
#cp -a "$SDL2_SO" "$LIB_DIR/libSDL2.so"
#cp -a "$DEX" "$BUILD_DIR/"

# It's "manifest madness time" !!! :-D
cat > "$MANIFEST" <<EOF
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    xmlns:tools="http://schemas.android.com/tools"
    package="$APP_PACKAGE"
    android:versionCode="$VERSION_CODE"
    android:versionName="$VERSION_NAME"
>
    <uses-sdk android:minSdkVersion="$MIN_SDK" android:targetSdkVersion="$TARGET_SDK" />
    <uses-permission android:name="android.permission.MANAGE_EXTERNAL_STORAGE" />
    <uses-permission android:name="android.permission.WRITE_EXTERNAL_STORAGE" android:maxSdkVersion="29" tools:ignore="ScopedStorage" />
    <application android:label="$APP_NAME" android:icon="@mipmap/ic_launcher" android:requestLegacyExternalStorage="true" android:debuggable="true" >
        <activity android:name="org.libsdl.app.SDLActivity"
                  android:configChanges="keyboard|keyboardHidden|orientation|screenSize"
                  android:theme="@android:style/Theme.NoTitleBar.Fullscreen"
                  android:launchMode="singleTask"
                  android:exported="true" >
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
        <meta-data android:name="author_name" android:value="LGB Gábor Lénárt" />
        <meta-data android:name="author_email" android:value="lgblgblgb@gmail.com" />
	<meta-data android:name="license" android:value="GPL (GNU GENERAL PUBLIC LICENSE) v2" />
        <meta-data android:name="website" android:value="https://github.com/lgblgblgb/xemu" />
    </application>
</manifest>
EOF

touch "$ASSETS_DIR/.placeholder"
mkdir -p "$RES_DIR/layout"
echo '<LinearLayout xmlns:android="http://schemas.android.com/apk/res/android"
    android:layout_width="fill_parent"
    android:layout_height="fill_parent" />' > "$RES_DIR/layout/dummy.xml"

# Lanucher icons
for r in mipmap-ldpi:36		\
	mipmap-mdpi:48		\
	mipmap-hdpi:72		\
	mipmap-xhdpi:96		\
	mipmap-xxhdpi:144	\
	mipmap-xxxhdpi:192
do
	dir="`echo $r | cut -f1 -d:`"
	res="`echo $r | cut -f2 -d:`"
	echo -n "Creating icon $RES_DIR/$dir/ic_launcher.png ($res pix) ="
	mkdir -p "$RES_DIR/$dir"
	convert -resize "${res}x${res}" "$ICON_ORIG" "$RES_DIR/$dir/ic_launcher.png"
	file "$RES_DIR/$dir/ic_launcher.png" | cut -f2- -d:
done

# This should create the initial ZIP and put the manifest file into the android "binary format"
aapt package -f -M "$MANIFEST" -I "$PLATFORM" -S "$RES_DIR" -A "$ASSETS_DIR" -F "$APK_UNSIGNED"

# Update ZIP with our specific .so files (and the dex)
cd "$BUILD_DIR"
zip -u unsigned.apk lib/$ARCH/libmain.so lib/$ARCH/libSDL2.so classes.dex
cd "$TARGET_DIR"

zipalign -f 4 "$APK_UNSIGNED" "$APK_ALIGNED"

KEYPASS_STR="`cat $KEYPASS | head -n 1 | tr -d '\n'`"
STOREPASS_STR="`cat $STOREPASS | head -n 1 | tr -d '\n'`"

if [ ! -s "$KEYSTORE" ]; then
	echo "Creating debug keystore $KEYSTORE ($SIGN_DNAME) using keypass in $KEYPASS and storepass in $STOREPASS"
	#keytool -genkey -v -keystore $KEYSTORE -keyalg RSA -keysize 2048 -validity 10000 -alias keypass -storepass "`cat $STOREPASS`" -keypass "`cat $KEYPASS`" -dname "CN=SDLApp"
	keytool -genkey -v			\
		-keystore "$KEYSTORE"		\
		-storetype JKS			\
		-alias myalias			\
		-keyalg RSA			\
		-keysize 2048			\
		-validity 10000			\
		-storepass "$STOREPASS_STR"	\
		-keypass "$KEYPASS_STR" 	\
		-dname "$SIGN_DNAME"		\
		-noprompt
	ls -l $KEYSTORE
fi

#apksigner sign --ks $KEYSTORE --ks-key-alias keypass --ks-pass "pass:`cat $STOREPASS`" --key-pass "pass:`cat $KEYPASS`" --out "$APK_FINAL" "$APK_ALIGNED"
apksigner sign \
	--ks "$KEYSTORE" \
	--ks-key-alias myalias \
	--ks-pass "pass:$STOREPASS_STR" \
	--key-pass "pass:$KEYPASS_STR" \
	--out "$APK_FINAL" "$APK_ALIGNED"

echo "APK built: $APK_FINAL"

APK_TARGET="$TARGET_DIR/$XEMU_TARGET.apk"

mv "$APK_FINAL"  "$APK_TARGET"

ls -l "$APK_TARGET"

exit 0
