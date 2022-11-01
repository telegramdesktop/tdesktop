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
class Thread;
} // namespace Data

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
	[[nodiscard]] virtual Data::Thread *cornerButtonsThread() = 0;
	[[nodiscard]] virtual FullMsgId cornerButtonsCurrentId() = 0;
	[[nodiscard]] virtual bool cornerButtonsIgnoreVisibility() = 0;
	[[nodiscard]] virtual std::optional<bool> cornerButtonsDownShown() = 0;
	[[nodiscard]] virtual bool cornerButtonsUnreadMayBeShown() = 0;
	[[nodiscard]] virtual bool cornerButtonsHas(CornerButtonType type) = 0;
};

class CornerButtons final : private QObject {
public:
	CornerButtons(
		not_null<Ui::ScrollArea*> parent,
		not_null<const Ui::ChatStyle*> st,
		not_null<CornerButtonsDelegate*> delegate);

	using Type = CornerButtonType;

	void downClick();
	void mentionsClick();
	void reactionsClick();

	void clearReplyReturns();
	[[nodiscard]] QVector<FullMsgId> replyReturns() const;
	void setReplyReturns(QVector<FullMsgId> replyReturns);
	void pushReplyReturn(not_null<HistoryItem*> item);
	void skipReplyReturn(FullMsgId id);
	void calculateNextReplyReturn();

	void updateVisibility(Type type, bool shown);
	void updateUnreadThingsVisibility();
	void updateJumpDownVisibility(std::optional<int> counter = {});
	void updatePositions();

	void finishAnimations();

	[[nodiscard]] HistoryItem *replyReturn() const {
		return _replyReturn;
	}
	[[nodiscard]] Fn<void(bool found)> doneJumpFrom(
		FullMsgId targetId,
		FullMsgId originId,
		bool ignoreMessageNotFound = false);

private:
	bool eventFilter(QObject *o, QEvent *e) override;

	void computeCurrentReplyReturn();

	[[nodiscard]] CornerButton &buttonByType(Type type);
	[[nodiscard]] History *lookupHistory() const;
	void showAt(MsgId id);

	const not_null<Ui::ScrollArea*> _scroll;
	const not_null<CornerButtonsDelegate*> _delegate;

	CornerButton _down;
	CornerButton _mentions;
	CornerButton _reactions;

	HistoryItem *_replyReturn = nullptr;
	QVector<FullMsgId> _replyReturns;

	bool _replyReturnStarted = false;

};

} // namespace HistoryView
