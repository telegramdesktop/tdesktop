/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "ui/effects/message_sending_animation_common.h"

namespace HistoryView {
class Element;
} // namespace HistoryView

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {

class RpWidget;
class ChatTheme;

class MessageSendingAnimationController final {
public:
	explicit MessageSendingAnimationController(
		not_null<Window::SessionController*> controller);

	struct SendingInfoTo {
		rpl::producer<QRect> globalEndGeometry;
		Fn<not_null<HistoryView::Element*>()> view;
		not_null<Ui::ChatTheme*> theme;
	};

	void appendSending(MessageSendingAnimationFrom from);
	void startAnimation(SendingInfoTo &&to);

	[[nodiscard]] bool hasLocalMessage(MsgId msgId) const;
	[[nodiscard]] bool hasAnimatedMessage(not_null<HistoryItem*> item) const;

private:

	const not_null<Window::SessionController*> _controller;
	base::flat_map<MsgId, QRect> _itemSendPending;

	base::flat_map<
		not_null<HistoryItem*>,
		base::unique_qptr<RpWidget>> _processing;

};

} // namespace Ui
