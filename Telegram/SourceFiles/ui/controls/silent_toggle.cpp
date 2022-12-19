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

namespace Ui {
namespace {

constexpr auto kAnimationDuration = crl::time(120);

} // namespace

SilentToggle::SilentToggle(QWidget *parent, not_null<ChannelData*> channel)
: RippleButton(parent, st::historySilentToggle.ripple)
, _st(st::historySilentToggle)
, _channel(channel)
, _checked(channel->owner().notifySettings().silentPosts(_channel)) {
	Expects(!channel->owner().notifySettings().silentPostsUnknown(_channel));

	resize(_st.width, _st.height);

	paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
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
	}, lifetime());

	setMouseTracking(true);
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
		_crossLineAnimation.start(
			[=] { update(); },
			_checked ? 0. : 1.,
			_checked ? 1. : 0.,
			kAnimationDuration);
	}
}

void SilentToggle::leaveEventHook(QEvent *e) {
	RippleButton::leaveEventHook(e);
	Ui::Tooltip::Hide();
}

void SilentToggle::mouseReleaseEvent(QMouseEvent *e) {
	setChecked(!_checked);
	RippleButton::mouseReleaseEvent(e);
	Ui::Tooltip::Show(0, this);
	_channel->owner().notifySettings().update(_channel, {}, _checked);
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
