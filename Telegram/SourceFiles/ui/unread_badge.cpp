/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/unread_badge.h"

#include "dialogs/dialogs_layout.h"

namespace Ui {

void UnreadBadge::setText(const QString &text, bool active) {
	_text = text;
	_active = active;
	update();
}

void UnreadBadge::paintEvent(QPaintEvent *e) {
	if (_text.isEmpty()) {
		return;
	}

	Painter p(this);

	Dialogs::Layout::UnreadBadgeStyle unreadSt;
	unreadSt.muted = !_active;
	auto unreadRight = width();
	auto unreadTop = 0;
	Dialogs::Layout::paintUnreadCount(
		p,
		_text,
		unreadRight,
		unreadTop,
		unreadSt);
}

} // namespace Ui
