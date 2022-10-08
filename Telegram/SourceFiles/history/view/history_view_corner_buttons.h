/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "base/object_ptr.h"

class History;
class HistoryItem;
struct FullMsgId;

namespace Ui {
class ChatStyle;
class ScrollArea;
class HistoryDownButton;
} // namespace Ui

namespace Data {
struct MessagePosition;
} // namespace Data

namespace Dialogs {
class Entry;
} // namespace Dialogs

namespace HistoryView {

struct CornerButton {
	template <typename ...Args>
	CornerButton(Args &&...args) : widget(std::forward<Args>(args)...) {
	}

	object_ptr<Ui::HistoryDownButton> widget;
	Ui::Animations::Simple animation;
	bool shown = false;
};

enum class CornerButtonType {
	Down,
	Mentions,
	Reactions,
};

class CornerButtonsDelegate {
public:
	virtual void cornerButtonsShowAtPosition(
		Data::MessagePosition position) = 0;
	[[nodiscard]] virtual Dialogs::Entry *cornerButtonsEntry() = 0;
	[[nodiscard]] virtual FullMsgId cornerButtonsCurrentId() = 0;
	[[nodiscard]] virtual bool cornerButtonsIgnoreVisibility() = 0;
	[[nodiscard]] virtual bool cornerButtonsDownShown() = 0;
	[[nodiscard]] virtual bool cornerButtonsUnreadMayBeShown() = 0;
};

class CornerButtons final : private QObject {
public:
	CornerButtons(
		not_null<Ui::ScrollArea*> parent,
		not_null<const Ui::ChatStyle*> st,
		not_null<CornerButtonsDelegate*> delegate);

	void downClick();
	void mentionsClick();
	void reactionsClick();

	void clearReplyReturns();
	[[nodiscard]] QVector<FullMsgId> replyReturns() const;
	void setReplyReturns(QVector<FullMsgId> replyReturns);
	void pushReplyReturn(not_null<HistoryItem*> item);
	void skipReplyReturn(FullMsgId id);
	void calculateNextReplyReturn();

	void updateVisibility(CornerButtonType type, bool shown);
	void updateUnreadThingsVisibility();
	void updateJumpDownVisibility(std::optional<int> counter = {});
	void updatePositions();

	void finishAnimations();

	[[nodiscard]] HistoryItem *replyReturn() const {
		return _replyReturn;
	}

	CornerButton down;
	CornerButton mentions;
	CornerButton reactions;

private:
	bool eventFilter(QObject *o, QEvent *e) override;

	void computeCurrentReplyReturn();

	[[nodiscard]] CornerButton &buttonByType(CornerButtonType type);
	[[nodiscard]] History *lookupHistory() const;
	void showAt(MsgId id);

	const not_null<Ui::ScrollArea*> _scroll;
	const not_null<CornerButtonsDelegate*> _delegate;

	HistoryItem *_replyReturn = nullptr;
	QVector<FullMsgId> _replyReturns;

};

} // namespace HistoryView
