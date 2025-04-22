#!/bin/bash

if [ "$TARGET_BRANCH" == "" ]; then
	echo "TARGET_BRANCH is empty" >&2
	exit 1
fi
if [ "$XEMU_DEPLOY_TOKEN" == "" ]; then
	echo "XEMU_DEPLOY_TOKEN is empty" >&2
	exit 1
fi
if [ "$COMMIT_MESSAGE" == "" ]; then
	echo "COMMIT_MESSAGE is empty" >&2
	exit 1
fi

echo "Deployment by $0 in directory `pwd` for target branch $TARGET_BRANCH with commit message of $COMMIT_MESSAGE"

touch .nojekyll
ls -l
git init || exit 1
git checkout --orphan "$TARGET_BRANCH" || exit 1
git add . || exit 1
git commit -m "$COMMIT_MESSAGE" || exit 1
git status
git push --force --quiet "https://x-access-token:$XEMU_DEPLOY_TOKEN@github.com/lgblgblgb/xemu-binaries.git" "$TARGET_BRANCH:refs/heads/$TARGET_BRANCH" || exit 1
git log

echo "Everything seems to be fine with script $0"

exit 0
