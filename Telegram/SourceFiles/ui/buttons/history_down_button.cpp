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

namespace Ui {

HistoryDownButton::HistoryDownButton(QWidget *parent) : AbstractButton(parent)
//, a_arrowOpacity(st::historyAttachEmoji.opacity, st::historyAttachEmoji.opacity)
, _a_arrowOver(animation(this, &HistoryDownButton::step_arrowOver)) {
	setCursor(style::cur_pointer);

	int iconWidth = st::historyToDown.width();
	int iconHeight = st::historyToDown.height();
	int retina = cIntRetinaFactor();
	resize(iconWidth, st::historyToDownPaddingTop + iconHeight);

	QImage cache(iconWidth * retina, iconHeight * retina, QImage::Format_ARGB32_Premultiplied);
	cache.setDevicePixelRatio(cRetinaFactor());
	{
		Painter p(&cache);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.fillRect(0, 0, iconWidth, iconHeight, Qt::transparent);
		st::historyToDown.paint(p, QPoint(0, 0), st::historyToDown.width());
	}
	_cache = App::pixmapFromImageInPlace(std_::move(cache));
	_cache.setDevicePixelRatio(cRetinaFactor());

	hide();
}

void HistoryDownButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (_a_show.animating(getms())) {
		p.setOpacity(_a_show.current());
		p.drawPixmap(0, st::historyToDownPaddingTop, _cache);
	} else if (!_shown) {
		hide();
		return;
	} else {
		st::historyToDown.paint(p, QPoint(0, st::historyToDownPaddingTop), width());
	}
	st::historyToDownArrow.paint(p, QPoint(0, st::historyToDownPaddingTop), width());
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

void HistoryDownButton::onStateChanged(int oldState, StateChangeSource source) {
	//a_arrowOpacity.start((_state & (StateOver | StateDown)) ? st::historyAttachEmoji.overOpacity : st::historyAttachEmoji.opacity);

	if (source == StateChangeSource::ByUser || source == StateChangeSource::ByPress) {
		_a_arrowOver.stop();
		a_arrowOpacity.finish();
		update();
	} else {
		_a_arrowOver.start();
	}
}

void HistoryDownButton::setUnreadCount(int unreadCount) {
	_unreadCount = unreadCount;
	update();
}

bool HistoryDownButton::hidden() const {
	return !_shown;
}

void HistoryDownButton::showAnimated() {
	if (_shown) return;

	if (isHidden()) show();
	toggleAnimated();
}

void HistoryDownButton::hideAnimated() {
	if (!_shown) return;
	toggleAnimated();
}

void HistoryDownButton::toggleAnimated() {
	_shown = !_shown;
	float64 from = _shown ? 0. : 1., to = _shown ? 1. : 0.;
	_a_show.start([this] { update(); }, from, to, st::historyToDownDuration);
}

void HistoryDownButton::finishAnimation() {
	_a_show.finish();
	setVisible(_shown);
}

void HistoryDownButton::step_arrowOver(float64 ms, bool timer) {
	float64 dt = ms / st::historyAttachEmoji.duration;
	if (dt >= 1) {
		_a_arrowOver.stop();
		a_arrowOpacity.finish();
	} else {
		a_arrowOpacity.update(dt, anim::linear);
	}
	if (timer) update();
}

EmojiButton::EmojiButton(QWidget *parent, const style::IconButton &st) : AbstractButton(parent)
, _st(st)
, _a_loading(animation(this, &EmojiButton::step_loading)) {
	resize(_st.width, _st.height);
	setCursor(style::cur_pointer);
}

void EmojiButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto ms = getms();

	p.fillRect(e->rect(), st::historyComposeAreaBg);

	auto loading = a_loading.current(ms, _loading ? 1 : 0);
	p.setOpacity(1 - loading);

	auto over = (_state & StateOver);
	auto icon = &(over ? _st.iconOver : _st.icon);
	icon->paint(p, (_state & StateDown) ? _st.iconPositionDown : _st.iconPosition, width());

	p.setOpacity(1.);
	p.setPen(QPen(over ? st::historyEmojiCircleFgOver : st::historyEmojiCircleFg, st::historyEmojiCircleLine));
	p.setBrush(Qt::NoBrush);

	p.setRenderHint(QPainter::HighQualityAntialiasing);
	QRect inner(QPoint((width() - st::historyEmojiCircle.width()) / 2, st::historyEmojiCircleTop), st::historyEmojiCircle);
	if (loading > 0) {
		int32 full = 5760;
		int32 start = qRound(full * float64(ms % uint64(st::historyEmojiCirclePeriod)) / st::historyEmojiCirclePeriod), part = qRound(loading * full / st::historyEmojiCirclePart);
		p.drawArc(inner, start, full - part);
	} else {
		p.drawEllipse(inner);
	}
	p.setRenderHint(QPainter::HighQualityAntialiasing, false);
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

void EmojiButton::onStateChanged(int oldState, StateChangeSource source) {
	auto over = (_state & StateOver);
	if (over != (oldState & StateOver)) {
		update();
	}
}

} // namespace Ui
