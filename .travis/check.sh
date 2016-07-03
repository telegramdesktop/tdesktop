#!/bin/bash
# Checks commit message, ...

run() {
	checkCommitMessage
}

checkCommitMessage() {
	info_msg "Commit message: ${TRAVIS_COMMIT_MSG}";
	info_msg "Is pull request: ${TRAVIS_PULL_REQUEST}";

	if [[ $TRAVIS_PULL_REQUEST != "false" ]];then
		if [[  $TRAVIS_COMMIT_MSG != *"Signed-off-by: "* ]];then
			error_msg "The commit message does not contain the signature!"
			error_msg "More information: https://github.com/telegramdesktop/tdesktop/blob/master/.github/CONTRIBUTING.md#sign-your-work"
			addMissingSignatureComment
			exit 1
		else
			success_msg "Commit message contains signature"
		fi
	fi
}

addMissingSignatureComment() {
	if [[ $BUILD_VERSION == "" ]]; then
		local TEXT="Hi,\n\
thanks for the pull request!\n\
\n\
Please read our [contributing policy](https://github.com/telegramdesktop/tdesktop/blob/master/.github/CONTRIBUTING.md). You'll need to make a pull request with the \\\"Signed-off-by:\\\" signature being the last line of your commit message, like it is described in [sign your work](https://github.com/telegramdesktop/tdesktop/blob/master/.github/CONTRIBUTING.md#sign-your-work) section. That will grant your work into the public domain.\n\
\n\
(See [travis build](https://travis-ci.org/telegramdesktop/tdesktop/jobs/${TRAVIS_JOB_ID}))"
		addCommentToGitHubPR "${TEXT}"
	fi
}

addCommentToGitHubPR() {
	local BODY=$1
	curl -H "Authorization: token ${GH_AUTH_TOKEN}" --data "{\"body\": \"${BODY}\"}" "https://api.github.com/repos/${TRAVIS_REPO_SLUG}/issues/${TRAVIS_PULL_REQUEST}/comments"
}

source ./.travis/common.sh

run
