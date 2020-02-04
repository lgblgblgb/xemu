#!/bin/bash
# (C)2016-2020 LGB Gabor Lenart
# https://github.com/lgblgblgb/xemu

set -e

if [ "$1" = "" -o "$2" = "" -o "$3" = "" ]; then
	echo "BAD USAGE"
	exit 1
fi

DIR="$1"
OUT="$2"

cd $DIR

rm -fr .zip
mkdir -p .zip

CMD="zip ../$OUT"

while [ "$3" != "" ]; do
	file=`basename "$3"`
	if [ ! -f "$file" ]; then
		echo "FILE NOT FOUND (in $DIR): $file"
		exit 1
	fi
	outfile=`echo "$file" | awk -F. '
	{
		if (NF < 2) {
			print
		} else {
			if (NF == 2 && ($NF == "native" || $NF == "osx")) {
				print $1
			} else if (NF == 2 && ($NF == "win32" || $NF == "win64")) {
				print $1 ".exe"
			} else {
				print $0
			}
		}
	}'`
	echo "$file -> .zip/$outfile"
	cp -a $file .zip/$outfile
	CMD="$CMD $outfile"
	shift
done

echo "cd .zip"
cd .zip
echo $CMD
$CMD

cd ..

rm -fr .zip

exit 0
