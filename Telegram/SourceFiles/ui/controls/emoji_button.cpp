/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/emoji_button.h"

#include "ui/effects/radial_animation.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "styles/style_chat_helpers.h"

namespace Ui {

EmojiButton::EmojiButton(QWidget *parent, const style::EmojiButton &st)
: RippleButton(parent, st.inner.ripple)
, _st(st) {
	resize(_st.inner.width, _st.inner.height);
	setCursor(style::cur_pointer);
}

void EmojiButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	p.fillRect(e->rect(), _st.bg);
	const auto &st = _st.inner;
	paintRipple(p, st.rippleAreaPosition.x(), st.rippleAreaPosition.y(), _rippleOverride ? &(*_rippleOverride)->c : nullptr);

	const auto over = isOver();
	const auto loadingState = _loading
		? _loading->computeState()
		: RadialState{ 0., 0, RadialState::kFull };
	const auto icon = _iconOverride ? _iconOverride : &(over ? st.iconOver : st.icon);
	auto position = st.iconPosition;
	if (position.x() < 0) {
		position.setX((width() - icon->width()) / 2);
	}
	if (position.y() < 0) {
		position.setY((height() - icon->height()) / 2);
	}
	const auto skipx = icon->width() / 4;
	const auto skipy = icon->height() / 4;
	const auto inner = QRect(
		position + QPoint(skipx, skipy),
		QSize(icon->width() - 2 * skipx, icon->height() - 2 * skipy));

	if (loadingState.shown < 1.) {
		p.setOpacity(1. - loadingState.shown);
		icon->paint(p, position, width());
		p.setOpacity(1.);
	}

	const auto color = (_colorOverride
		? *_colorOverride
		: (over ? _st.lineFgOver : _st.lineFg));
	const auto line = style::ConvertScaleExact(st::historyEmojiCircleLine);
	if (anim::Disabled() && _loading && _loading->animating()) {
		anim::DrawStaticLoading(p, inner, line, color);
	} else {
		auto pen = color->p;
		pen.setWidthF(line);
		pen.setCapStyle(Qt::RoundCap);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);

		PainterHighQualityEnabler hq(p);
		if (loadingState.arcLength < RadialState::kFull) {
			p.drawArc(inner, loadingState.arcFrom, loadingState.arcLength);
		} else {
			p.drawEllipse(inner);
		}
	}
}

void EmojiButton::loadingAnimationCallback() {
	if (!anim::Disabled()) {
		update();
	}
}

void EmojiButton::setLoading(bool loading) {
	if (loading && !_loading) {
		_loading = std::make_unique<InfiniteRadialAnimation>(
			[=] { loadingAnimationCallback(); },
			st::defaultInfiniteRadialAnimation);
	}
	if (loading) {
		_loading->start();
		update();
	} else if (_loading) {
		_loading->stop();
		update();
	}
}

void EmojiButton::setColorOverrides(const style::icon *iconOverride, const style::color *colorOverride, const style::color *rippleOverride) {
	_iconOverride = iconOverride;
	_colorOverride = colorOverride;
	_rippleOverride = rippleOverride;
	update();
}

void EmojiButton::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	auto wasOver = static_cast<bool>(was & StateFlag::Over);
	if (isOver() != wasOver) {
		update();
	}
}

QPoint EmojiButton::prepareRippleStartPosition() const {
	if (!_st.inner.rippleAreaSize) {
		return DisabledRippleStartPosition();
	}
	return mapFromGlobal(QCursor::pos()) - _st.inner.rippleAreaPosition;
}

QImage EmojiButton::prepareRippleMask() const {
	const auto size = _st.inner.rippleAreaSize;
	return RippleAnimation::EllipseMask(QSize(size, size));
}

} // namespace Ui
