/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/menu/menu_add_action_callback.h"

class DocumentData;

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace EmojiStatusMenu {

void AddSetAsStatusAction(
	const Ui::Menu::MenuCallback &addAction,
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<DocumentData*> document,
	const style::icon *icon);

} // namespace EmojiStatusMenu
