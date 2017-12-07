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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "profile/profile_back_button.h"

//#include "history/history_top_bar_widget.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"
#include "styles/style_profile.h"
#include "styles/style_info.h"

namespace Profile {

BackButton::BackButton(QWidget *parent, const QString &text) : Ui::AbstractButton(parent)
, _text(text.toUpper()) {
	setCursor(style::cur_pointer);

	subscribe(Adaptive::Changed(), [this] { updateAdaptiveLayout(); });
	updateAdaptiveLayout();
}

void BackButton::setText(const QString &text) {
	_text = text.toUpper();
	update();
}

int BackButton::resizeGetHeight(int newWidth) {
	return st::profileTopBarHeight;
}

void BackButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(e->rect(), st::profileBg);
	st::topBarBack.paint(p, (st::topBarArrowPadding.left() - st::topBarBack.width()) / 2, (st::topBarHeight - st::topBarBack.height()) / 2, width());

	p.setFont(st::topBarButton.font);
	p.setPen(st::topBarButton.textFg);
	p.drawTextLeft(st::topBarArrowPadding.left(), st::topBarButton.padding.top() + st::topBarButton.textTop, width(), _text);

//	HistoryTopBarWidget::paintUnreadCounter(p, width());
}

void BackButton::onStateChanged(State was, StateChangeSource source) {
	if (isDown() && !(was & StateFlag::Down)) {
		emit clicked();
	}
}

void BackButton::updateAdaptiveLayout() {
	if (!Adaptive::OneColumn()) {
		unsubscribe(base::take(_unreadCounterSubscription));
	} else if (!_unreadCounterSubscription) {
		_unreadCounterSubscription = subscribe(Global::RefUnreadCounterUpdate(), [this] {
			rtlupdate(0, 0, st::titleUnreadCounterRight, st::titleUnreadCounterTop);
		});
	}
}

} // namespace Profile
