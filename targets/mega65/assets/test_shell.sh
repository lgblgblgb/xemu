#!/usr/bin/env bash
cd `dirname $0`/.. || exit 1
exec gmake web HTML=assets/shell.html BROWSER="firefox --new-window"
