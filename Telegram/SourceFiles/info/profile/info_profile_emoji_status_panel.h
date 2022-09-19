/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"

namespace Data {
class Session;
enum class CustomEmojiSizeTag : uchar;
} // namespace Data

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
struct MessageSendingAnimationFrom;
class RpWidget;
} // namespace Ui

namespace Ui::Text {
class CustomEmoji;
struct CustomEmojiPaintContext;
} // namespace Ui::Text

namespace ChatHelpers {
class TabbedPanel;
} // namespace ChatHelpers

namespace Info::Profile {

class EmojiStatusPanel final {
public:
	EmojiStatusPanel();
	~EmojiStatusPanel();

	void show(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> button,
		Data::CustomEmojiSizeTag animationSizeTag = {});

	bool paintBadgeFrame(not_null<Ui::RpWidget*> widget);

private:
	class Animation;

	void create(not_null<Window::SessionController*> controller);

	void startAnimation(
		not_null<Data::Session*> owner,
		not_null<Ui::RpWidget*> body,
		DocumentId statusId,
		Ui::MessageSendingAnimationFrom from);

	base::unique_qptr<ChatHelpers::TabbedPanel> _panel;
	QPointer<QWidget> _panelButton;
	std::unique_ptr<Animation> _animation;
	Data::CustomEmojiSizeTag _animationSizeTag = {};

};

} // namespace Info::Profile
