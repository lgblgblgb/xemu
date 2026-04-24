#!/usr/bin/env bash
BROWSER="firefox --new-window"
if [ "$1" != "" ]; then
	BROWSER="$1"
fi
cd `dirname $0`/.. || exit 1
cp assets/shell.{gif,js,css} ../../build/bin/ || exit 1
convert ../../build/xemu-48x48.xpm ../../build/bin/favicon.ico || exit 1
exec gmake web HTML=assets/shell.html BROWSER="$BROWSER"
