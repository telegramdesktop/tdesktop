/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_top_bar_action_button.h"

#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "lottie/lottie_icon.h"
#include "styles/style_layers.h"
#include "styles/style_info.h"

namespace Info::Profile {
namespace {

constexpr auto kIconFadeStart = 0.4;
constexpr auto kIconFadeRange = 1.0 - kIconFadeStart;

} // namespace

TopBarActionButton::TopBarActionButton(
	not_null<QWidget*> parent,
	const QString &text,
	const QString &lottieName)
: RippleButton(parent, st::universalRippleAnimation)
, _text(text) {
	setupLottie(lottieName);
}

TopBarActionButton::TopBarActionButton(
	not_null<QWidget*> parent,
	const QString &text,
	const style::icon &icon)
: RippleButton(parent, st::universalRippleAnimation)
, _text(text)
, _icon(&icon) {
}

TopBarActionButton::~TopBarActionButton() = default;

void TopBarActionButton::setupLottie(const QString &lottieName) {
	_lottie = std::make_unique<Lottie::Icon>(Lottie::IconDescriptor{
		.name = lottieName,
		.color = _lottieColor,
		.sizeOverride = Size(st::infoProfileTopBarActionButtonLottieSize),
	});
	_lottie->animate([=] { update(); }, 0, _lottie->framesCount() - 1);
}

void TopBarActionButton::convertToToggle(
		const style::icon &offIcon,
		const style::icon &onIcon,
		const QString &offLottie,
		const QString &onLottie) {
	_isToggle = true;
	_offIcon = &offIcon;
	_onIcon = &onIcon;
	_offLottie = offLottie;
	_onLottie = onLottie;
	_icon = _offIcon;
	_lottie.reset();
}

void TopBarActionButton::toggle(bool state) {
	if (!_isToggle || _toggleState == state) {
		return;
	}
	_toggleState = state;
	const auto &lottie = _toggleState ? _onLottie : _offLottie;
	setupLottie(lottie);
	_lottie->animate([=] {
		update();
		if (_lottie->frameIndex() == _lottie->framesCount() - 1) {
			_icon = _toggleState ? _onIcon : _offIcon;
			_lottie.reset();
		}
	}, 0, _lottie->framesCount() - 1);
}

void TopBarActionButton::finishAnimating() {
	if (_lottie) {
		_icon = _toggleState ? _onIcon : _offIcon;
		_lottie.reset();
		update();
	}
}

void TopBarActionButton::setText(const QString &text) {
	_text = text;
	update();
}

void TopBarActionButton::setLottieColor(const style::color *color) {
	_lottieColor = color;
	_lottie.reset();
	update();
}

void TopBarActionButton::setStyle(const TopBarActionButtonStyle &style) {
	_bgColor = style.bgColor;
	_fgColor = style.fgColor;
	_shadowColor = style.shadowColor;
	update();
}

void TopBarActionButton::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	const auto progress = float64(height())
		/ st::infoProfileTopBarActionButtonSize;
	p.setOpacity(progress);

	p.setPen(Qt::NoPen);
	p.setBrush(_bgColor);
	{
		auto hq = PainterHighQualityEnabler(p);
		// Todo shadows.
		p.drawRoundedRect(rect(), st::boxRadius, st::boxRadius);
	}

	paintRipple(p, 0, 0);

	const auto iconSize = st::infoProfileTopBarActionButtonIconSize;
	const auto iconTop = st::infoProfileTopBarActionButtonIconTop;

	if (_lottie || _icon) {
		const auto iconScale = (progress > kIconFadeStart)
			? (progress - kIconFadeStart) / kIconFadeRange
			: 0.0;
		p.setOpacity(iconScale);
		p.save();
		const auto iconLeft = (width() - iconSize) / 2.;
		const auto half = iconSize / 2.;
		const auto iconCenter = QPointF(iconLeft + half, iconTop + half);
		p.translate(iconCenter);
		p.scale(iconScale, iconScale);
		p.translate(-iconCenter);
		p.translate(iconLeft, iconTop);
		if (_lottie) {
			_lottie->paint(p, 0, 0, _fgColor);
		} else if (_icon) {
			if (_fgColor) {
				_icon->paint(p, 0, 0, width(), *_fgColor);
			} else {
				_icon->paint(p, 0, 0, width());
			}
		}
		p.restore();
		p.setOpacity(progress);
	}

	const auto skip = st::infoProfileTopBarActionButtonTextSkip;

	p.setClipRect(0, 0, width(), height() - skip);

	if (_fgColor.has_value()) {
		p.setPen(*_fgColor);
	} else {
		p.setPen(st::windowBoldFg);
	}

	p.setFont(st::infoProfileTopBarActionButtonFont);

	const auto textScale = std::max(kIconFadeStart, progress);
	const auto textRect = rect()
		- QMargins(0, st::infoProfileTopBarActionButtonTextTop, 0, 0);
	const auto textCenter = rect::center(textRect);
	p.translate(textCenter);
	p.scale(textScale, textScale);
	p.translate(-textCenter);

	const auto elidedText = st::infoProfileTopBarActionButtonFont->elided(
		_text,
		textRect.width(),
		Qt::ElideMiddle);
	p.drawText(textRect, elidedText, style::al_top);
}

QImage TopBarActionButton::prepareRippleMask() const {
	return Ui::RippleAnimation::RoundRectMask(size(), st::boxRadius);
}

QPoint TopBarActionButton::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

} // namespace Info::Profile
