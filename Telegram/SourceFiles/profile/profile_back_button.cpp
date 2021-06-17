/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "profile/profile_back_button.h"

//#include "history/view/history_view_top_bar_widget.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"
#include "styles/style_profile.h"
#include "styles/style_info.h"

namespace Profile {

BackButton::BackButton(
	QWidget *parent,
	not_null<Main::Session*> session,
	const QString &text,
	rpl::producer<bool> oneColumnValue)
: Ui::AbstractButton(parent)
, _session(session)
, _text(text.toUpper()) {
	setCursor(style::cur_pointer);

	std::move(
		oneColumnValue
	) | rpl::start_with_next([=](bool oneColumn) {
		if (!oneColumn) {
			_unreadBadgeLifetime.destroy();
		} else if (!_unreadBadgeLifetime) {
			_session->data().unreadBadgeChanges(
			) | rpl::start_with_next([=] {
				rtlupdate(
					0,
					0,
					st::titleUnreadCounterRight,
					st::titleUnreadCounterTop);
			}, _unreadBadgeLifetime);
		}
	}, lifetime());
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
}

void BackButton::onStateChanged(State was, StateChangeSource source) {
	if (isDown() && !(was & StateFlag::Down)) {
		clicked(Qt::KeyboardModifiers(), Qt::LeftButton);
	}
}

} // namespace Profile
