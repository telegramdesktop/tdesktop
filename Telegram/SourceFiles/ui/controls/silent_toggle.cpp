/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/silent_toggle.h"

#include "ui/effects/ripple_animation.h"
#include "data/notify/data_notify_settings.h"
#include "data/data_session.h"
#include "data/data_channel.h"
#include "lang/lang_keys.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"

namespace Ui {

SilentToggle::SilentToggle(QWidget *parent, not_null<ChannelData*> channel)
: RippleButton(parent, st::historySilentToggle.ripple)
, _st(st::historySilentToggle)
, _channel(channel)
, _checked(channel->owner().notifySettings().silentPosts(_channel)) {
	Expects(!channel->owner().notifySettings().silentPostsUnknown(_channel));

	resize(_st.width, _st.height);

	setMouseTracking(true);

	clicks(
	) | rpl::start_with_next([=] {
		setChecked(!_checked);
		Ui::Tooltip::Show(0, this);
		_channel->owner().notifySettings().update(_channel, {}, _checked);
	}, lifetime());
}

void SilentToggle::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	paintRipple(p, _st.rippleAreaPosition, nullptr);

	//const auto checked = _crossLineAnimation.value(_checked ? 1. : 0.);
	const auto over = isOver();
	(_checked
		? (over
			? st::historySilentToggleOnOver
			: st::historySilentToggleOn)
		: (over
			? st::historySilentToggle.iconOver
			: st::historySilentToggle.icon)).paintInCenter(p, rect());
}

void SilentToggle::mouseMoveEvent(QMouseEvent *e) {
	RippleButton::mouseMoveEvent(e);
	if (rect().contains(e->pos())) {
		Ui::Tooltip::Show(1000, this);
	} else {
		Ui::Tooltip::Hide();
	}
}

void SilentToggle::setChecked(bool checked) {
	if (_checked != checked) {
		_checked = checked;
		update();
		// _crossLineAnimation.start(
		// 	[=] { update(); },
		// 	_checked ? 0. : 1.,
		// 	_checked ? 1. : 0.,
		// 	kAnimationDuration);
	}
}

void SilentToggle::leaveEventHook(QEvent *e) {
	RippleButton::leaveEventHook(e);
	Ui::Tooltip::Hide();
}

QString SilentToggle::tooltipText() const {
	return _checked
		? tr::lng_wont_be_notified(tr::now)
		: tr::lng_will_be_notified(tr::now);
}

QPoint SilentToggle::tooltipPos() const {
	return QCursor::pos();
}

bool SilentToggle::tooltipWindowActive() const {
	return Ui::AppInFocus() && InFocusChain(window());
}

QPoint SilentToggle::prepareRippleStartPosition() const {
	const auto result = mapFromGlobal(QCursor::pos())
		- _st.rippleAreaPosition;
	const auto rect = QRect(0, 0, _st.rippleAreaSize, _st.rippleAreaSize);
	return rect.contains(result)
		? result
		: DisabledRippleStartPosition();
}

QImage SilentToggle::prepareRippleMask() const {
	return RippleAnimation::EllipseMask(
		QSize(_st.rippleAreaSize, _st.rippleAreaSize));
}

} // namespace Ui
