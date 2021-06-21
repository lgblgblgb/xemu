#!/bin/bash
# ------------------------------------------------------------------------------------------------------------------------------------------------
# The original file has been downloaded from https://github.com/DiscordHooks/travis-ci-discord-webhook
# Copyright: (C) 2017 Sankarsan Kampa, according to the MIT license: https://github.com/DiscordHooks/travis-ci-discord-webhook/blob/master/LICENSE
# ------------------------------------------------------------------------------------------------------------------------------------------------
# Also, this file has been HEAVILY modified by me for fitting to my needs in the Xemu project: (C)2021 Gabor Lenart (aka LGB)
# ------------------------------------------------------------------------------------------------------------------------------------------------

BOT_NAME="XEMU Builder"
BOT_NAME_FUNNY="XEMU (body-)Builder"
#BOT_AVATAR="https://travis-ci.org/images/logos/TravisCI-Mascot-1.png"
BOT_AVATAR="https://lgblgblgb.github.io/xemu/images/xemu-48x48.png"
AUTHOR_DISCORD_ID="731142195851034704"

echo "[DISCORD] Starting ${BOT_NAME} discord trigger with parameter $1 in directory `pwd` on host `hostname`"

cd `dirname $0`/../.. || exit 1
XEMU_VERSION="$(cat build/objs/cdate.data)"
if [ "$XEMU_VERSION" = "" ]; then
	echo "[DISCORD] ERROR: no build cdate info? File: build/objs/cdate.data" >&2
	exit 1
fi

echo "[DISCORD] XEMU version (cdate) is ${XEMU_VERSION}"

case $1 in
	"building" )
		EMBED_COLOR=15105570
		STATUS_MESSAGE="Building"
		AVATAR="https://travis-ci.org/images/logos/TravisCI-Mascot-red.png"
		;;

	"success" )
		EMBED_COLOR=3066993
		STATUS_MESSAGE="Passed"
		AVATAR="https://travis-ci.org/images/logos/TravisCI-Mascot-blue.png"
		;;

	"failure" )
		EMBED_COLOR=15158332
		STATUS_MESSAGE="Failed"
		AVATAR="https://travis-ci.org/images/logos/TravisCI-Mascot-red.png"
		;;

	* )
		EMBED_COLOR=0
		STATUS_MESSAGE="Status Unknown"
		AVATAR="https://travis-ci.org/images/logos/TravisCI-Mascot-1.png"
		;;
esac

shift

if [ $# -lt 1 ]; then
	echo "[DISCORD] ERROR: Missing second parameter: build architecture" >&2
	exit 1
fi
BUILD_ARCH="$1"

shift

if [ $# -lt 1 ]; then
	echo "[DISCORD] ERROR: Missing branch(es):webhook-variable specification(s)" >&2
	exit 1
fi



# ---------------------------------------------------------------------------------------------
# Assume, this script is used on Travis
# Use GITHUB workflows to get information instead, if available
if [ "$TRAVIS_BRANCH" == "" -a "$GITHUB_REF" != "" ]; then
	TRAVIS_BRANCH="$(echo $GITHUB_REF | awk -F/ '{ print $NF }')"
	BUILDER_CI="Github"
fi
# If either of those, try to use local parameters
if [ "$TRAVIS_COMMIT" == "" ]; then
	TRAVIS_COMMIT="$(git log -1 --pretty="%H")"
	if [ "$BUILDER_CI" == "" ]; then
		BUILDER_CI="Unknown"
	fi
else
	if [ "$BUILDER_CI" == "" ]; then
		BUILDER_CI="Travis"
	fi
fi
if [ "$TRAVIS_BRANCH" == "" ]; then
	TRAVIS_BRANCH="$(git branch | awk 'BEGIN { s = "UNKNOWN" } $1 == "*" { s = $2 } END { print s }')"
fi
if [ "$TRAVIS_PULL_REQUEST" == "" ]; then
	TRAVIS_PULL_REQUEST="false"
fi
if [ "$TRAVIS_REPO_SLUG" == "" ]; then
	# Ehmm, kind of lame ...
	TRAVIS_REPO_SLUG="$(git config --get remote.origin.url | egrep -o '[^/]+/[^/]+$' | sed 's/\.git$//')"
fi
# ---------------------------------------------------------------------------------------------
XEMU_VERSION="$XEMU_VERSION/$TRAVIS_BRANCH"
echo "[DISCORD] current branch is ${TRAVIS_BRANCH} commit is ${TRAVIS_COMMIT} XEMU version is ${XEMU_VERSION}"
# ---------------------------------------------------------------------------------------------
# End of madness


AUTHOR_NAME="$(git log -1 "$TRAVIS_COMMIT" --pretty="%aN")"
COMMITTER_NAME="$(git log -1 "$TRAVIS_COMMIT" --pretty="%cN")"
COMMIT_SUBJECT="$(git log -1 "$TRAVIS_COMMIT" --pretty="%s")"
COMMIT_MESSAGE="$(git log -1 "$TRAVIS_COMMIT" --pretty="%b")" | sed -E ':a;N;$!ba;s/\r{0,1}\n/\\n/g'

if [ ${#COMMIT_SUBJECT} -gt 256 ]; then
	COMMIT_SUBJECT="$(echo "$COMMIT_SUBJECT" | cut -c 1-253)"
	COMMIT_SUBJECT+="..."
fi

if [ -n $COMMIT_MESSAGE ] && [ ${#COMMIT_MESSAGE} -gt 1900 ]; then
	COMMIT_MESSAGE="$(echo "$COMMIT_MESSAGE" | cut -c 1-1900)"
	COMMIT_MESSAGE+="..."
fi

if [ "$AUTHOR_NAME" == "$COMMITTER_NAME" ]; then
	CREDITS="$AUTHOR_NAME authored & committed"
else
	CREDITS="$AUTHOR_NAME authored & $COMMITTER_NAME committed"
fi

if [ "$TRAVIS_PULL_REQUEST" != "false" ]; then
	URL="https://github.com/$TRAVIS_REPO_SLUG/pull/$TRAVIS_PULL_REQUEST"
	echo "[DISCORD] No webhook activation for pull requests currently" >&2
	exit 0
else
	URL=""
fi


TIMESTAMP=$(date -u +%FT%TZ)


MSG=":desktop:  New Xemu build version **${XEMU_VERSION}** for **${BUILD_ARCH}** is now ***[on-line](<https://lgblgblgb.github.io/xemu/>)!***"
# Branch based decisions
MSG="${MSG} :scientist: "
if [ "$TRAVIS_BRANCH" == "master" ]; then
	MSG="${MSG}This is kind-of-**stable** (branch: **${TRAVIS_BRANCH}**) build, intended for _general use_."
elif [ "$TRAVIS_BRANCH" == "next" ]; then
	MSG="${MSG}This is next/**to-be-stable** with possible problems (branch: **${TRAVIS_BRANCH}**) build, so _you have been warned_, but you're more than welcome if you want to _help testing Xemu by using this branch_."
elif [ "$TRAVIS_BRANCH" == "dev" ]; then
	MSG="${MSG}This is **development** (branch: **${TRAVIS_BRANCH}**) build, it may ~~overclock your robot vacuum cleaner while trying to upgrade it into the Skynet~~ _won't work at all_."
elif [ "$TRAVIS_BRANCH" == "hmw" ]; then
	MSG="${MSG}This is **VIC-IV experimental** (branch: **${TRAVIS_BRANCH}**) build, it may ~~cause The Burn of all the VIC-IVs in the visible universe~~ _have problems, and lacks of other features of the other branches_."
else
	MSG="${MSG}This is \\\"**secret**\\\" not-for-general-use (branch: **${TRAVIS_BRANCH}**) build, ~~you don't want to even know about~~ _you want to be **extremely** careful with_."
fi
# Details about the build
MSG="$MSG :zap: See git commit [**\`${TRAVIS_COMMIT:0:7}\`**](<https://github.com/${TRAVIS_REPO_SLUG}/commit/${TRAVIS_COMMIT}>)"
if [ "$TRAVIS_JOB_WEB_URL" != "" ]; then
	MSG="$MSG and the [build log](<${TRAVIS_JOB_WEB_URL}>)"
fi
MSG="${MSG}, built by ${BUILDER_CI}-CI"
MSG="${MSG}. :calendar: _${BOT_NAME_FUNNY} on behalf of <@${AUTHOR_DISCORD_ID}> at ${TIMESTAMP}_"


WEBHOOK_DATA='{ "username": "'$BOT_NAME'", "avatar_url": "'$BOT_AVATAR'", "content": "'$MSG'" }'

#echo $WEBHOOK_DATA
#exit 0

USER_AGENT="TravisCI-Webhook"
CONTENT_TYPE="Content-Type:application/json"
X_AUTHOR="X-Author:Xemu"


for ARG in $@ ; do
	VALID_BRANCHES="$(echo "$ARG" | cut -d':' -f1)"
	WEBHOOK_URL_VAR="$(echo "$ARG" | cut -d':' -f2)"
	#echo "LGB allow-list=(${VALID_BRANCHES}) var-name=(${WEBHOOK_URL_VAR})"
	if [ "$VALID_BRANCHES" == "" -o "$WEBHOOK_URL_VAR" == "" -o "$WEBHOOK_URL_VAR" == "$VALID_BRANCHES" ]; then
		echo "[DISCORD] [${ARG}] ERROR: invalid specification for branch(es):webhook_url_var part. Skipping."
		continue
	fi
	if [ "$VALID_BRANCHES" == "ANY" ]; then
		echo "[DISCORD] [${ARG}] 'ANY' was specified for branch-list, treating as an always OK, not checking branch now."
	else
		if ! echo ",$VALID_BRANCHES," | grep -q ",$TRAVIS_BRANCH," ; then
			echo "[DISCORD] [${ARG}] REJECT: This branch (${TRAVIS_BRANCH}) was not in the allow-list (${VALID_BRANCHES}) for this WEBHOOK (${WEBHOOK_URL_VAR})."
			continue
		fi
		echo "[DISCORD] [${ARG}] branch (${TRAVIS_BRANCH}) was accepted according the allow-list (${VALID_BRANCHES})"
	fi
	echo "[DISCORD] [${ARG}] Triggering Discord's webhook ${WEBHOOK_URL_VAR} for branch ${TRAVIS_BRANCH} according to list of ${VALID_BRANCHES} ..."
	# Dear reader! Surely, /etc/lgb/discord-webhooks.txt is on my own computer for testing :) So you can stop trying to spy on that somewhere :)
	THE_URL="$(grep "^${WEBHOOK_URL_VAR}=" /etc/lgb/discord-webhooks.txt 2>/dev/null | tail -1 | cut -d'=' -f2-)"
	if [ "$THE_URL" == "" ]; then
		THE_URL="${!WEBHOOK_URL_VAR}"
	fi
	if [ "$THE_URL" != "" ]; then
		( curl --fail --progress-bar -A "${USER_AGENT}" -H "${CONTENT_TYPE}" -H "${X_AUTHOR}" -d "${WEBHOOK_DATA}" "${THE_URL}" && echo "[DISCORD] [${ARG}] Successfully sent :-)" ) || echo "[DISCORD] [${ARG}] ERROR: Unable to send :-("
	else
		echo "[DISCORD] [${ARG}] ERROR: Variable ${WEBHOOK_URL_VAR} cannot be found"
	fi
	THE_URL=""
done

exit 0
