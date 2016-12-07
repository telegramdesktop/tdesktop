/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "ui/buttons/history_down_button.h"

#include "styles/style_history.h"
#include "dialogs/dialogs_layout.h"
#include "ui/effects/ripple_animation.h"

namespace Ui {

HistoryDownButton::HistoryDownButton(QWidget *parent, const style::TwoIconButton &st) : RippleButton(parent, st.ripple)
, _st(st) {
	resize(_st.width, _st.height);
	setCursor(style::cur_pointer);

	hide();
}

QImage HistoryDownButton::prepareRippleMask() const {
	return Ui::RippleAnimation::ellipseMask(QSize(_st.rippleAreaSize, _st.rippleAreaSize));
}

QPoint HistoryDownButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos()) - _st.rippleAreaPosition;
}

void HistoryDownButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();
	auto over = isOver();
	auto down = isDown();
	((over || down) ? _st.iconBelowOver : _st.iconBelow).paint(p, _st.iconPosition, width());
	paintRipple(p, _st.rippleAreaPosition.x(), _st.rippleAreaPosition.y(), ms);
	((over || down) ? _st.iconAboveOver : _st.iconAbove).paint(p, _st.iconPosition, width());
	if (_unreadCount > 0) {
		auto unreadString = QString::number(_unreadCount);
		if (unreadString.size() > 4) {
			unreadString = qsl("..") + unreadString.mid(unreadString.size() - 4);
		}

		Dialogs::Layout::UnreadBadgeStyle st;
		st.align = style::al_center;
		st.font = st::historyToDownBadgeFont;
		st.size = st::historyToDownBadgeSize;
		st.sizeId = Dialogs::Layout::UnreadBadgeInHistoryToDown;
		Dialogs::Layout::paintUnreadCount(p, unreadString, width(), 0, st, nullptr);
	}
}

void HistoryDownButton::setUnreadCount(int unreadCount) {
	_unreadCount = unreadCount;
	update();
}

EmojiButton::EmojiButton(QWidget *parent, const style::IconButton &st) : RippleButton(parent, st.ripple)
, _st(st)
, _a_loading(animation(this, &EmojiButton::step_loading)) {
	resize(_st.width, _st.height);
	setCursor(style::cur_pointer);
}

void EmojiButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();

	p.fillRect(e->rect(), st::historyComposeAreaBg);
	paintRipple(p, _st.rippleAreaPosition.x(), _st.rippleAreaPosition.y(), ms);

	auto loading = a_loading.current(ms, _loading ? 1 : 0);
	p.setOpacity(1 - loading);

	auto over = isOver();
	auto icon = &(over ? _st.iconOver : _st.icon);
	icon->paint(p, _st.iconPosition, width());

	p.setOpacity(1.);
	auto pen = (over ? st::historyEmojiCircleFgOver : st::historyEmojiCircleFg)->p;
	pen.setWidth(st::historyEmojiCircleLine);
	pen.setCapStyle(Qt::RoundCap);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);

	PainterHighQualityEnabler hq(p);
	QRect inner(QPoint((width() - st::historyEmojiCircle.width()) / 2, st::historyEmojiCircleTop), st::historyEmojiCircle);
	if (loading > 0) {
		int32 full = FullArcLength;
		int32 start = qRound(full * float64(ms % st::historyEmojiCirclePeriod) / st::historyEmojiCirclePeriod), part = qRound(loading * full / st::historyEmojiCirclePart);
		p.drawArc(inner, start, full - part);
	} else {
		p.drawEllipse(inner);
	}
}

void EmojiButton::setLoading(bool loading) {
	if (_loading != loading) {
		_loading = loading;
		auto from = loading ? 0. : 1., to = loading ? 1. : 0.;
		a_loading.start([this] { update(); }, from, to, st::historyEmojiCircleDuration);
		if (loading) {
			_a_loading.start();
		} else {
			_a_loading.stop();
		}
	}
}

void EmojiButton::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	auto wasOver = static_cast<bool>(was & StateFlag::Over);
	if (isOver() != wasOver) {
		update();
	}
}

QPoint EmojiButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos()) - _st.rippleAreaPosition;
}

QImage EmojiButton::prepareRippleMask() const {
	return RippleAnimation::ellipseMask(QSize(_st.rippleAreaSize, _st.rippleAreaSize));
}

} // namespace Ui
