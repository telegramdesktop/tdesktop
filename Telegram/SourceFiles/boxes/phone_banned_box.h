/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/exteraGramDesktop/exteraGramDesktop/blob/dev/LEGAL
*/
#pragma once

namespace Window {
class Controller;
} // namespace Window

namespace Ui {

void ShowPhoneBannedError(
	not_null<Window::Controller*> controller,
	const QString &phone);

} // namespace Ui
