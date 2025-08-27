/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/jump_down_button.h"

#include "ui/effects/ripple_animation.h"
#include "ui/unread_badge_paint.h"
#include "styles/style_chat_helpers.h"

namespace Ui {

JumpDownButton::JumpDownButton(
	QWidget *parent,
	const style::TwoIconButton &st)
: RippleButton(parent, st.ripple)
, _st(st) {
	resize(_st.width, _st.height);
	setCursor(style::cur_pointer);

	hide();
}

QImage JumpDownButton::prepareRippleMask() const {
	return Ui::RippleAnimation::EllipseMask(
		QSize(_st.rippleAreaSize, _st.rippleAreaSize));
}

QPoint JumpDownButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos()) - _st.rippleAreaPosition;
}

void JumpDownButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto over = isOver();
	const auto down = isDown();
	((over || down)
		? _st.iconBelowOver
		: _st.iconBelow).paint(p, _st.iconPosition, width());
	paintRipple(p, _st.rippleAreaPosition.x(), _st.rippleAreaPosition.y());
	((over || down)
		? _st.iconAboveOver
		: _st.iconAbove).paint(p, _st.iconPosition, width());
	if (_unreadCount > 0) {
		auto unreadString = QString::number(_unreadCount);

		Ui::UnreadBadgeStyle st;
		st.align = style::al_center;
		st.font = st::historyToDownBadgeFont;
		st.size = st::historyToDownBadgeSize;
		st.sizeId = Ui::UnreadBadgeSize::HistoryToDown;
		Ui::PaintUnreadBadge(p, unreadString, width(), 0, st, 4);
	}
}

void JumpDownButton::setUnreadCount(int unreadCount) {
	if (_unreadCount != unreadCount) {
		_unreadCount = unreadCount;
		update();
	}
}

} // namespace Ui
