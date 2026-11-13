/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "base/weak_qptr.h"

class UserData;

namespace ChatHelpers {
class Show;
class TabbedPanel;
} // namespace ChatHelpers

namespace Ui {
class EmojiButton;
class GenericBox;
class InputField;
} // namespace Ui

class BotInfoEmojiPanel final {
public:
	BotInfoEmojiPanel(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<UserData*> bot);
	~BotInfoEmojiPanel();

	void attach(not_null<Ui::InputField*> field);

private:
	void aim(
		not_null<Ui::InputField*> field,
		not_null<Ui::EmojiButton*> toggle);

	const not_null<QWidget*> _outer;
	const not_null<UserData*> _bot;
	const base::unique_qptr<ChatHelpers::TabbedPanel> _panel;
	base::weak_qptr<Ui::InputField> _field;

};
