# Contributing

We love pull requests from everyone.
Push to your fork and [submit a pull request][pr].

Wait for us. We try to review your pull requests as fast as possible.
If we find issues with your pull request, we may suggest some changes and improvements.

**Table of Contents**

* [Build instructions](#build-instructions)
* [Pull upstream changes into your fork regularly](#pull-upstream-changes-into-your-fork-regularly)
* [How to get your pull request accepted](#how-to-get-your-pull-request-accepted)
	* [Keep your pull requests limited to a single issue](#keep-your-pull-requests-limited-to-a-single-issue)
	* [Don't mix code changes with whitespace cleanup](#dont-mix-code-changes-with-whitespace-cleanup)
	* [Keep your code simple!](#keep-your-code-simple)
	* [Test your changes!](#test-your-changes)
	* [Write a good commit message](#write-a-good-commit-message)

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

We want your submission. But we also want to provide a stable experience for our users and the community. Follow these rules and you should succeed without a problem!

### Keep your pull requests limited to a single issue

Pull requests should be as small/atomic as possible. Large, wide-sweeping changes in a pull request will be **rejected**, with comments to isolate the specific code in your pull request. Some examples:

* If you are making spelling corrections in the docs, don't modify other files.
* If you are adding new functions don't '*cleanup*' unrelated functions. That cleanup belongs in another pull request.

### Don't mix code changes with whitespace cleanup

If you change two lines of code and correct 200 lines of whitespace issues in a file the diff on that pull request is functionally unreadable and will be **rejected**. Whitespace cleanups need to be in their own pull request.

### Keep your code simple!
Please keep your code as clean and straightforward as possible.
When we see more than one or two functions/methods starting with `_my_special_function` or things like `__builtins__.object = str` we start to get worried.
Rather than try and figure out your brilliant work we'll just **reject** it and send along a request for simplification.

Furthermore, the pixel shortage is over. We want to see:

* `package` instead of `pkg`
* `grid` instead of `g`
* `my_function_that_does_things` instead of `mftdt`

### Test your changes!

Before you submit a pull request, please test your changes. Verify that Telegram Desktop still works and your changes don't cause other issue or crashes.

### Write a good commit message
Explain why you make the changes. [More infos about a good commit message.][commit_message]
Maybe reference also the related issue in your commit message.

[//]: # (LINKS)
[msvc]: MSVC.md
[xcode]: XCODE.md
[xcode_old]: XCODEold.md
[qtcreator]: qtcreator.md

[help_fork_repo]: https://help.github.com/articles/fork-a-repo/

[commit_message]: http://tbaggery.com/2008/04/19/a-note-about-git-commit-messages.html
[pr]: ../../compare/