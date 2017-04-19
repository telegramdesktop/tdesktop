#!/bin/bash
# Checks if the commit message contains the signature

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
            exit 1
        else
            success_msg "Commit message contains signature"
        fi
    fi
}

source ./.travis/common.sh

run
