# Contributing

This document describes how you can contribute to Telegram Desktop. Please read it carefully.

**Table of Contents**

* [What contributions are accepted](#what-contributions-are-accepted)
* [Sign your work](#sign-your-work)
  * [Change commit message of a pushed commit](#change-commit-message-of-a-pushed-commit)
* [Build instructions](#build-instructions)
* [Pull upstream changes into your fork regularly](#pull-upstream-changes-into-your-fork-regularly)
* [How to get your pull request accepted](#how-to-get-your-pull-request-accepted)
  * [Keep your pull requests limited to a single issue](#keep-your-pull-requests-limited-to-a-single-issue)
    * [Squash your commits to a single commit](#squash-your-commits-to-a-single-commit)
  * [Don't mix code changes with whitespace cleanup](#dont-mix-code-changes-with-whitespace-cleanup)
  * [Keep your code simple!](#keep-your-code-simple)
  * [Test your changes!](#test-your-changes)
  * [Write a good commit message](#write-a-good-commit-message)

## What contributions are accepted

We highly appreciate your contributions in the matter of fixing bugs and optimizing the Telegram Desktop source code and its documentation. In case of fixing the existing user experience please push to your fork and [submit a pull request][pr].

Wait for us. We try to review your pull requests as fast as possible.
If we find issues with your pull request, we may suggest some changes and improvements.

Unfortunately we **do not merge** any pull requests that have new feature implementations, translations to new languages and those which introduce any new user interface elements.

Telegram Desktop is not a standalone application but a part of [Telegram project][telegram], so all the decisions about the features, languages, user experience, user interface and the design are made inside Telegram team, often according to some roadmap which is not public.

## Sign your work

For contributions to be accepted they should be granted into the public domain. This will solve the issue if Telegram team needs to use full Telegram Desktop source code with some different license.

The sign-off is a simple line at the end of the explanation for the patch. Your signature certifies that you wrote the patch and you have the right to put it in the public domain. The rules are pretty simple: if you can certify the below:

```
Telegram Desktop Developer Certificate of Origin

By making a contribution to this project, I certify that:

(a) The contribution was created in whole by me or is based upon
    previous work that, to the best of my knowledge, is in the
    public domain and I have the right to put it in the public domain.

(d) I understand and agree that this project and the contribution are
    public and that a record of the contribution (including all
    metadata and personal information I submit with it, including my
    sign-off) is maintained indefinitely and may be redistributed.

(e) I am granting this work into the public domain.
```

Then you just add a line to every **git commit message** that states:

    Signed-off-by: Random J Developer <random@developer.example.org> (github: rndjdev_github)

Replacing Random Developer’s details with your name, email address and GitHub username.

### Change commit message of a pushed commit

If you already pushed a commit and forgot to add the signature to the commit message, follow these steps to change the message of the commit:

1. Open `Git Bash` (or `Git Shell`)
2. Enter following command to change the commit message of the most recent commit: `git commit --amend`
3. Press <kbd>i</kbd> to get into Insert-mode
4. Change the commit message (and add the [signature](#sign-your-work) at the and)
5. After editing the message, press <kbd>ESC</kbd> to get out of the Insert-mode
6. Write `:wq` and press <kbd>Enter</kbd> to save the new message or write `:q!` to discard your changes
7. Enter `git push --force` to push the commit with the new commit message to the remote repository

For more info, see [GitHub Help][help_change_commit_message].

## Build instructions

* [Visual Studio 2013][msvc]
* [XCode 6.4][xcode]
* [XCode 6.4 for OS X 10.6 and 10.7][xcode_old]
* [Qt Creator 3.2.0 Ubuntu][qtcreator]

## Pull upstream changes into your fork regularly

Telegram Desktop is advancing quickly. It is therefore critical that you pull upstream changes into your fork on a regular basis. Nothing is worse than putting in a days of hard work into a pull request only to have it rejected because it has diverged too far from upstram.

To pull in upstream changes:

    git remote add upstream https://github.com/telegramdesktop/tdesktop.git
    git fetch upstream master

Check the log to be sure that you actually want the changes, before merging:

    git log upstream/master

Then merge the changes that you fetched:

    git merge upstream/master

For more info, see [GitHub Help][help_fork_repo].

## How to get your pull request accepted

We want to improve Telegram Desktop with your contributions. But we also want to provide a stable experience for our users and the community. Follow these rules and you should succeed without a problem!

### Keep your pull requests limited to a single issue

Pull requests should be as small/atomic as possible. Large, wide-sweeping changes in a pull request will be **rejected**, with comments to isolate the specific code in your pull request. Some examples:

* If you are making spelling corrections in the docs, don't modify other files.
* If you are adding new functions don't '*cleanup*' unrelated functions. That cleanup belongs in another pull request.

#### Squash your commits to a single commit

To keep the history of the project clean, you should make one commit per pull request.
If you already have multiple commits, you can add the commits together (squash them) with the following commands in Git Bash:

1. Open `Git Bash` (or `Git Shell`)
2. Enter following command to squash the recent {N} commits: `git reset --soft HEAD~{N} && git commit` (replace `{N}` with the number of commits you want to squash)
3. Press <kbd>i</kbd> to get into Insert-mode
4. Enter the commit message of the new commit (and add the [signature](#sign-your-work) at the and)
5. After adding the message, press <kbd>ESC</kbd> to get out of the Insert-mode
6. Write `:wq` and press <kbd>Enter</kbd> to save the new message or write `:q!` to discard your changes
7. Enter `git push --force` to push the new commit to the remote repository

For example, if you want to squash the last 5 commits, use `git reset --soft HEAD~5 && git commit`

### Don't mix code changes with whitespace cleanup

If you change two lines of code and correct 200 lines of whitespace issues in a file the diff on that pull request is functionally unreadable and will be **rejected**. Whitespace cleanups need to be in their own pull request.

### Keep your code simple!

Please keep your code as clean and straightforward as possible.
Furthermore, the pixel shortage is over. We want to see:

* `opacity` instead of `o`
* `placeholder` instead of `ph`
* `myFunctionThatDoesThings()` instead of `mftdt()`

### Test your changes!

Before you submit a pull request, please test your changes. Verify that Telegram Desktop still works and your changes don't cause other issue or crashes.

### Write a good commit message

Explain why you make the changes. [More infos about a good commit message.][commit_message]
Maybe reference also the related issue in your commit message.
Don't forget to [sign your patch](#sign-your-work) to put it in the public domain.

[//]: # (LINKS)
[telegram]: https://telegram.org/
[msvc]: MSVC.md
[xcode]: XCODE.md
[xcode_old]: XCODEold.md
[qtcreator]: QTCREATOR.md
[help_fork_repo]: https://help.github.com/articles/fork-a-repo/
[help_change_commit_message]: https://help.github.com/articles/changing-a-commit-message/
[commit_message]: http://tbaggery.com/2008/04/19/a-note-about-git-commit-messages.html
[pr]: ../../compare/
