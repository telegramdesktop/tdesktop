/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/exteraGramDesktop/exteraGramDesktop/blob/dev/LEGAL
*/
#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace UserpicBuilder {

struct Result;

void AddEmojiBuilderAction(
	not_null<Window::SessionController*> controller,
	not_null<Ui::PopupMenu*> menu,
	rpl::producer<std::vector<DocumentId>> documents,
	Fn<void(UserpicBuilder::Result)> &&done,
	bool isForum);

} // namespace UserpicBuilder
