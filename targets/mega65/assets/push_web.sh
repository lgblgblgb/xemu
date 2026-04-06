#!/usr/bin/env bash
TARGET="lgb:/var/www/virtual/xemu.lgb.hu/test"
BDIR="../../build/bin"
cd `dirname $0`/.. || exit 1
NAME="`sed -n '/^PRG_TARGET[[:space:]]*=[[:space:]]*/ { s///p; q; }' Makefile`"
scp assets/shell.html $TARGET/index.html
scp ../../build/xemu-48x48.xpm $TARGET/favicon.ico
FILES="assets/shell.gif assets/shell.js assets/shell.css"
RMLIST=""
for a in data html js wasm ; do
	echo "Compressing file: $BDIR/$NAME.$a"
	gzip -9 < $BDIR/$NAME.$a > $BDIR/$NAME.$a.gz || exit 1
	FILES="$FILES $BDIR/$NAME.$a $BDIR/$NAME.$a.gz"
	RMLIST="$RMLIST $BDIR/$NAME.$a.gz"
done
echo "Files to scp: $FILES"
scp $FILES $TARGET/
echo "Files to delete: $RMLIST"
rm $RMLIST
