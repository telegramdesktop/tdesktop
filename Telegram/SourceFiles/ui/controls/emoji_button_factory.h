/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace ChatHelpers {
class TabbedPanel;
} // namespace ChatHelpers

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {

class BoxContent;
class EmojiButton;
class InputField;

[[nodiscard]] not_null<Ui::EmojiButton*> AddEmojiToggleToField(
	not_null<Ui::InputField*> field,
	not_null<Ui::BoxContent*> box,
	not_null<Window::SessionController*> controller,
	not_null<ChatHelpers::TabbedPanel*> emojiPanel,
	QPoint shift);

} // namespace Ui
