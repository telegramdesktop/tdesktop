/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "ui/text/text_custom_emoji.h"
#include "data/data_message_reaction_id.h"

namespace Ui {

class PopupMenu;

struct WhoReadParticipant {
	QString name;
	QString date;
	bool dateReacted = false;
	bool self = false;
	QString customEntityData;
	Data::ReactionId reaction;
	QImage userpicSmall;
	QImage userpicLarge;
	std::pair<uint64, uint64> userpicKey = {};
	uint64 id = 0;

	static constexpr auto kMaxSmallUserpics = 3;
};

bool operator==(const WhoReadParticipant &a, const WhoReadParticipant &b);
bool operator!=(const WhoReadParticipant &a, const WhoReadParticipant &b);

enum class WhoReadType {
	Seen,
	Listened,
	Watched,
	Reacted,
	Edited,
	Original,
};

enum class WhoReadState : uchar {
	Empty,
	Unknown,
	MyHidden,
	HisHidden,
	TooOld,
};

struct WhoReadContent {
	std::vector<WhoReadParticipant> participants;
	WhoReadType type = WhoReadType::Seen;
	QString singleCustomEntityData;
	int fullReactionsCount = 0;
	int fullReadCount = 0;
	WhoReadState state = WhoReadState::Empty;
};

[[nodiscard]] base::unique_qptr<Menu::ItemBase> WhoReactedContextAction(
	not_null<PopupMenu*> menu,
	rpl::producer<WhoReadContent> content,
	Text::CustomEmojiFactory factory,
	Fn<void(WhoReadParticipant)> participantChosen,
	Fn<void()> showAllChosen,
	Fn<void(WhoReadParticipant)> moderateReactionChosen = nullptr);

[[nodiscard]] base::unique_qptr<Menu::ItemBase> WhenReadContextAction(
	not_null<PopupMenu*> menu,
	rpl::producer<WhoReadContent> content,
	Fn<void()> showOrPremium = nullptr);

enum class WhoReactedType : uchar {
	Viewed,
	Reacted,
	Reposted,
	Forwarded,
	Preloader,
	RefRecipient,
	RefRecipientNow,
};

struct WhoReactedEntryData {
	QString text;
	QString date;
	WhoReactedType type = WhoReactedType::Viewed;
	QString customEntityData;
	QImage userpic;
	Fn<void()> callback;
	Fn<void()> closeCallback;
};

class WhoReactedEntryAction final : public Menu::ItemBase {
public:
	using Data = WhoReactedEntryData;

	WhoReactedEntryAction(
		not_null<Ui::Menu::Menu*> parent,
		Text::CustomEmojiFactory factory,
		const style::Menu &st,
		Data &&data);

	void setData(Data &&data);

	not_null<QAction*> action() const override;
	bool isEnabled() const override;

private:
	int contentHeight() const override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	QPoint prepareRippleStartPosition() const override;
	QImage prepareRippleMask() const override;

	void paint(Painter &&p);
	[[nodiscard]] bool closeAffordanceActive() const;
	void refreshCloseMouseTracking();
	void refreshCloseGeometry();
	void updateCloseHovered(QPoint globalPosition);
	void clearCloseState();
	void invalidateCloseCache();

	const not_null<QAction*> _dummyAction;
	const Text::CustomEmojiFactory _customEmojiFactory;
	const style::Menu &_st;
	const int _height = 0;

	Text::String _text;
	Text::String _date;
	std::unique_ptr<Ui::Text::CustomEmoji> _custom;
	QImage _userpic;
	int _textWidth = 0;
	int _customSize = 0;
	WhoReactedType _type = WhoReactedType::Viewed;
	Fn<void()> _closeCallback;
	QRect _closeRect;
	bool _closeHovered = false;
	bool _closePressed = false;
	bool _closeRippleActive = false;
	mutable QImage _closeBadge;
	mutable QImage _closeBadgeMask;

};

class WhoReactedListMenu final {
public:
	WhoReactedListMenu(
		Text::CustomEmojiFactory factory,
		Fn<void(WhoReadParticipant)> participantChosen,
		Fn<void()> showAllChosen,
		Fn<void(WhoReadParticipant)> moderateReactionChosen = nullptr);

	void clear();
	void populate(
		not_null<PopupMenu*> menu,
		const WhoReadContent &content,
		Fn<void()> refillTopActions = nullptr,
		int addedToBottom = 0,
		Fn<void()> appendBottomActions = nullptr);

private:
	const Text::CustomEmojiFactory _customEmojiFactory;
	const Fn<void(WhoReadParticipant)> _participantChosen;
	const Fn<void()> _showAllChosen;
	const Fn<void(WhoReadParticipant)> _moderateReactionChosen;

	std::vector<not_null<WhoReactedEntryAction*>> _actions;

};

} // namespace Ui
