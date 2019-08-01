#!/bin/bash
# A very lame NSIS installer stuff, Windows/NSI expert is wanted ...
# (C)2018,2019 LGB Gabor Lenart

cd `dirname $0` || exit 1
echo "$0: current directory: `pwd`"

dll="`./system-config $1 sdl2 dll`"

if [ "$dll" = "" -o ! -s "$dll" ]; then
	echo "Bad DLL for arch=$1: $dll" >&2
	exit 1
fi

installer="install-xemu-$1.exe"
zipfile="xemu-$1-binaries.zip"
echo "$0: target installer: $installer"
echo "$0: target ZIP file:  $zipfile"
echo "$0: using DLL:        $dll"
zipcmd="`basename $dll` README.txt LICENSE.txt"

cp -a "$dll" bin/ || exit 1
cp -a ../LICENSE bin/LICENSE.txt || exit 1
cp -a ../README.md bin/README.txt || exit 1

size=`stat -c '%s' "$dll"`

for a in bin/*.$1 ; do
	exe="`echo $a | sed 's/\.win[0-9]*$/.exe/'`"
	echo "$0: selecting binary: $exe based on $a"
	rm -f $exe || exit 1
	cp $a $exe || exit 1
	size=$((size+`stat -c '%s' $exe`))
	zipcmd="$zipcmd `basename $exe`"
done

convert xemu-32x28.xpm bin/xemu.ico

wine ~/.wine/drive_c/Program\ Files\ \(x86\)/NSIS/makensis.exe /D`echo $1 | tr 'a-z' 'A-Z'` /DSDL2DLL=`basename $dll` /DEXENAME=$installer /DARCH=$1 /DXEMUVER=`date '+%Y.%m.%d.%H%M'` /DVERSIONMAJOR=0 /DVERSIONMINOR=`date '+%Y%m%d%H%M'` /DVERSIONBUILD=0 /DINSTALLSIZE=$((size/1024)) xemu.nsi || exit 1
ls -l bin/$installer

cd bin || exit 1

echo "ZIPPING (to $zipfile) with command line: zip $zipfile $zipcmd"
rm -f $zipfile
zip $zipfile $zipcmd || exit 1
ls -l $zipfile
rm -f $zipcmd
exit 0
