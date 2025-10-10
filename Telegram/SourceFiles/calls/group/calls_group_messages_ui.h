/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/round_rect.h"

struct TextWithTags;

namespace ChatHelpers {
class Show;
class TabbedPanel;
} // namespace ChatHelpers

namespace Data {
struct ReactionId;
} // namespace Data

namespace Ui {
class ElasticScroll;
class EmojiButton;
class InputField;
class SendButton;
class RpWidget;
} // namespace Ui

namespace Calls::Group {

struct Message;

class MessagesUi final {
public:
	MessagesUi(
		not_null<QWidget*> parent,
		std::shared_ptr<ChatHelpers::Show> show,
		rpl::producer<std::vector<Message>> messages,
		rpl::producer<bool> shown);
	~MessagesUi();

	void move(int left, int bottom, int width, int availableHeight);
	void raise();

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	struct MessageView;

	void setupList(
		rpl::producer<std::vector<Message>> messages,
		rpl::producer<bool> shown);
	void toggleMessage(MessageView &entry, bool shown);
	void setContentFailed(MessageView &entry);
	void setContent(MessageView &entry, const TextWithEntities &text);
	void updateMessageSize(MessageView &entry);
	bool updateMessageHeight(MessageView &entry);
	void animateMessageSent(MessageView &entry);
	void repaintMessage(uint64 id);
	void recountHeights(std::vector<MessageView>::iterator i, int top);
	void appendMessage(const Message &data);
	void checkReactionContent(
		MessageView &entry,
		const TextWithEntities &text);
	void startReactionAnimation(MessageView &entry);
	void updateReactionPosition(MessageView &entry);
	void removeReaction(not_null<Ui::RpWidget*> widget);
	void setupMessagesWidget();
	void applyWidth();
	void updateGeometries();
	void updateTopFade();
	void updateBottomFade();

	const not_null<QWidget*> _parent;
	const std::shared_ptr<ChatHelpers::Show> _show;
	std::unique_ptr<Ui::ElasticScroll> _scroll;
	Ui::Animations::Simple _scrollToBottomAnimation;
	Ui::RpWidget *_messages = nullptr;
	QImage _canvas;

	std::vector<MessageView> _views;
	style::complex_color _messageBg;
	Ui::RoundRect _messageBgRect;

	QPoint _reactionBasePosition;
	rpl::lifetime _effectsLifetime;

	Ui::Animations::Simple _topFadeAnimation;
	Ui::Animations::Simple _bottomFadeAnimation;
	int _fadeHeight = 0;
	bool _topFadeShown = false;
	bool _bottomFadeShown = false;

	int _left = 0;
	int _bottom = 0;
	int _width = 0;
	int _availableHeight = 0;

	uint64 _revealedSpoilerId = 0;

	rpl::lifetime _lifetime;

};

} // namespace Calls::Group
