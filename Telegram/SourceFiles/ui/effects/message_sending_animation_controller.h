/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "ui/effects/animation_value.h"
#include "ui/effects/message_sending_animation_common.h"

namespace HistoryView {
class Element;
} // namespace HistoryView

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {

class RpWidget;
struct ChatPaintContext;

struct MessageSendingReveal {
	crl::time duration = 0;
	anim::transition transition = anim::linear;
};

class MessageSendingAnimationController final {
public:
	explicit MessageSendingAnimationController(
		not_null<Window::SessionController*> controller);

	struct SendingTarget {
		Fn<std::optional<QPoint>(bool live)> globalTopLeft;
		rpl::producer<> changes;
		rpl::producer<> revealChanges;
		Fn<HistoryView::Element*()> view;
		Fn<Ui::ChatPaintContext()> paintContext;
	};
	struct SendingInfoTo {
		rpl::producer<std::optional<QPoint>> globalEndTopLeft;
		rpl::producer<std::optional<QPoint>> globalLiveTopLeft;
		Fn<HistoryView::Element*()> view;
		Fn<Ui::ChatPaintContext()> paintContext;
	};

	void appendSending(MessageSendingAnimationFrom from);
	[[nodiscard]] std::optional<MessageSendingReveal> startAnimation(
		SendingTarget &&target,
		bool textFlightOnly = false);
	void startPendingTextFlights(
		const std::vector<not_null<HistoryView::Element*>> &views,
		Fn<void(not_null<HistoryItem*>)> start);
	void dropPendingSnapshot(not_null<HistoryItem*> item);

	[[nodiscard]] bool hasAnimatedMessage(not_null<HistoryItem*> item) const;
	[[nodiscard]] bool checkExpectedType(not_null<HistoryItem*> item);

	void clear();

private:
	struct Pending {
		MessageSendingAnimationFrom from;
		base::unique_qptr<RpWidget> snapshot;
	};

	void subscribeToDestructions();

	const not_null<Window::SessionController*> _controller;
	base::flat_map<MsgId, Pending> _itemSendPending;

	base::flat_map<
		not_null<HistoryItem*>,
		base::unique_qptr<RpWidget>> _processing;

	rpl::lifetime _lifetime;

};

} // namespace Ui
