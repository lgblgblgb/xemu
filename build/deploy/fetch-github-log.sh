#!/bin/bash

if [ "$TRAVIS_REPO_SLUG" == "" ]; then
	echo "TRAVIS_REPO_SLUG is empty" >&2
	exit 1
fi
if [ "$TRAVIS_BRANCH" == "" ]; then
	echo "TRAVIS_BRANCH is empty" >&2
	exit 1
fi
if [ "$1" == "" ]; then
	PER_PAGE=25
else
	PER_PAGE="$1"
fi

curl -s "https://api.github.com/repos/$TRAVIS_REPO_SLUG/commits?sha=$TRAVIS_BRANCH&per_page=$PER_PAGE" | \
jq -r '.[] |
"commit \(.sha)\nAuthor: \(.commit.author.name) <\(.commit.author.email)>\nDate:   \(.commit.author.date | fromdate | strflocaltime("%a %b %d %T %Y %z"))\n\n    \(.commit.message | gsub("\n"; "\n    "))\n"'

exit 0
