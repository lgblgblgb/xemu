#!/usr/bin/env bash
TARGET="lgb:/var/www/virtual/xemu.lgb.hu/test"
BDIR="../../build/bin"
cd `dirname $0`/.. || exit 1
NAME="`sed -n '/^PRG_TARGET[[:space:]]*=[[:space:]]*/ { s///p; q; }' Makefile`"
scp assets/shell.html $TARGET/index.html
scp assets/shell.gif $TARGET/
for a in data html js wasm ; do
	gzip -9 < $BDIR/$NAME.$a > $BDIR/$NAME.$a.gz || exit 1
	scp $BDIR/$NAME.$a $BDIR/$NAME.$a.gz $TARGET/
	rm -f $BDIR/$NAME.$a.gz
done
scp ../../build/xemu-48x48.xpm $TARGET/favicon.ico
