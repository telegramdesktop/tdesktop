/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"

struct TextWithTags;

namespace ChatHelpers {
class Show;
class TabbedPanel;
} // namespace ChatHelpers

namespace Ui {
class EmojiButton;
class InputField;
class SendButton;
class RpWidget;
} // namespace Ui

namespace Calls::Group {

class ReactionPanel;

class MessageField final {
public:
	MessageField(
		not_null<QWidget*> parent,
		std::shared_ptr<ChatHelpers::Show> show,
		PeerData *peer);
	~MessageField();

	void resizeToWidth(int newWidth);
	void move(int x, int y);
	void toggle(bool shown);
	void raise();

	[[nodiscard]] int height() const;
	[[nodiscard]] rpl::producer<int> heightValue() const;

	[[nodiscard]] rpl::producer<TextWithTags> submitted() const;
	[[nodiscard]] rpl::producer<> closeRequests() const;
	[[nodiscard]] rpl::producer<> closed() const;

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void createControls(PeerData *peer);
	void setupBackground();
	void shownAnimationCallback();
	void updateEmojiPanelGeometry();
	void updateWrapSize(int widthOverride = 0);
	void updateHeight();

	const not_null<QWidget*> _parent;
	const std::shared_ptr<ChatHelpers::Show> _show;
	const std::unique_ptr<Ui::RpWidget> _wrap;

	int _limit = 0;
	Ui::InputField *_field = nullptr;
	Ui::SendButton *_send = nullptr;
	Ui::EmojiButton *_emojiToggle = nullptr;
	std::unique_ptr<ChatHelpers::TabbedPanel> _emojiPanel;
	std::unique_ptr<ReactionPanel> _reactionPanel;
	rpl::variable<bool> _fieldFocused;
	rpl::variable<bool> _fieldEmpty = true;

	rpl::variable<int> _width;
	rpl::variable<int> _height;

	bool _shown = false;
	Ui::Animations::Simple _shownAnimation;
	std::unique_ptr<Ui::RpWidget> _cache;

	rpl::event_stream<TextWithTags> _submitted;
	rpl::event_stream<> _closeRequests;
	rpl::event_stream<> _closed;

	rpl::lifetime _lifetime;

};

} // namespace Calls::Group
