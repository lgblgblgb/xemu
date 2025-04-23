#!/bin/bash
# (C)2020,2021 Gabor Lenart LGB lgblgblgb@gmail.com

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
cat README.md > $TARGET/README-XEMU.md
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
	env | grep -E '^(TRAVIS|GITHUB)_' | grep -v '=$' | grep -Evi 'secure|hook|secret|token|path|_id|_sha' | sort | sed 's/=/ = /' | sed 's/^/* /'
	echo
	echo "## Commit log (last 10)"
	echo
	build/deploy/fetch-github-log.sh 10 | sed 's/^/    /'
) > $TARGET/README.md

cd $TARGET

# These renames are needed, as the download page needs a constant file name, while deb/rpm
# package names varies with each version. So if rename them. This can be run with any deployment,
# in case eg windows, it won't find any deb/rpm files for sure.
# Rename DEB if any ...
b="xemu_current_amd64.deb"
for a in *.deb ; do
	if [ -f "$a" ]; then
		echo "Found DEB: $a -> $b"
		if [ "$b" = "" ]; then
			echo "ERROR, another DEB found?" >&2
			exit 1
		fi
		mv "$a" "$b"
		chmod 644 "$b"
		b=""
	fi
done
if [ "$b" != "" ]; then
	echo "No DEB found (not a Linux build?)"
fi
# Rename RPM if any ...
b="xemu-current-1.x86_64.rpm"
for a in *.rpm ; do
	if [ -f "$a" ]; then
		echo "Found RPM: $a -> $b"
		if [ "$b" = "" ]; then
			echo "ERROR, another RPM found?" >&2
			exit 1
		fi
		mv "$a" "$b"
		chmod 644 "$b"
		b=""
	fi
done
if [ "$b" != "" ]; then
	echo "No RPM found (not a Linux build?)"
fi

md5sum * > ../MD5
echo "Checksums:"
cat ../MD5
mv ../MD5 .

cd ..

exit 0
