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
class EmojiFlyAnimation;
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

	void setChooseFilter(Fn<bool(DocumentId)> filter);

	void show(
		not_null<Window::SessionController*> controller,
		not_null<QWidget*> button,
		Data::CustomEmojiSizeTag animationSizeTag = {});

	struct Descriptor {
		not_null<Window::SessionController*> controller;
		not_null<QWidget*> button;
		Data::CustomEmojiSizeTag animationSizeTag = {};
		DocumentId currentBackgroundEmojiId = 0;
		Fn<QColor()> customTextColor;
		bool backgroundEmojiMode = false;
	};
	void show(Descriptor &&descriptor);
	void repaint();

	[[nodiscard]] rpl::producer<DocumentId> backgroundEmojiChosen() const {
		return _backgroundEmojiChosen.events();
	}

	bool paintBadgeFrame(not_null<Ui::RpWidget*> widget);

private:
	void create(const Descriptor &descriptor);
	[[nodiscard]] bool filter(
		not_null<Window::SessionController*> controller,
		DocumentId chosenId) const;

	void startAnimation(
		not_null<Data::Session*> owner,
		not_null<Ui::RpWidget*> body,
		DocumentId statusId,
		Ui::MessageSendingAnimationFrom from);

	base::unique_qptr<ChatHelpers::TabbedPanel> _panel;
	Fn<QColor()> _customTextColor;
	Fn<bool(DocumentId)> _chooseFilter;
	QPointer<QWidget> _panelButton;
	std::unique_ptr<Ui::EmojiFlyAnimation> _animation;
	rpl::event_stream<DocumentId> _backgroundEmojiChosen;
	Data::CustomEmojiSizeTag _animationSizeTag = {};
	bool _backgroundEmojiMode = false;

};

} // namespace Info::Profile
