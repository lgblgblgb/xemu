#!/bin/bash
# (C)2020 Gabor Lenart LGB lgblgblgb@gmail.com

if [ "$1" = "" -o "$2" = "" ]; then
	echo "Bad usage."
	exit 1
fi

set -e

TARGET="$1"
ARCH="$2"

mkdir -p $TARGET
cp * $TARGET/ || true
rm $TARGET/Makefile
cp README.md $TARGET/README-XEMU.md
cat build/objs/cdate.data > $TARGET/versioninfo

(
	if [ "$TRAVIS_BRANCH" = "master" ]; then
		SED_RULE=".*$"
	else
		SED_RULE=""
	fi
	sed "s/%ARCH%/$ARCH/" < build/deploy/template.md | sed "s/%BRANCH%/$TRAVIS_BRANCH/" | sed "s/^IFNOTMASTER:$SED_RULE//" | sed "s/%VERSION%/`cat $TARGET/versioninfo`/"
	echo "* **BUILD_COMMIT = https://github.com/$TRAVIS_REPO_SLUG/commit/$TRAVIS_COMMIT**"
	echo "* **BUILD_DATE = `date`**"
	echo "* **BUILD_GIT_REMOTE = `git config --get remote.origin.url` (branch: $TRAVIS_BRANCH)**"
	echo "* **BUILD_LOG_URL = $TRAVIS_JOB_WEB_URL**"
	echo "* **BUILD_OS = ($TRAVIS_OS_NAME) `uname -a`**"
	echo "* **BUILD_TARGET = $ARCH**"
	echo "* **BUILD_UPTIME = `uptime`**"
	env | grep '^TRAVIS_' | grep -v '=$' | grep -vi secure | sort | sed 's/=/ = /' | sed 's/^/* /'
	echo
	echo "## Commit log (last 25)"
	echo
	git log -25
) > $TARGET/README.md

cd $TARGET
md5sum * > ../MD5
cd ..
mv MD5 $TARGET/

exit 0
