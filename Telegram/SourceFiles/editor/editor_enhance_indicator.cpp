/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_enhance_indicator.h"

#include "lang/lang_keys.h"
#include "styles/style_basic.h"
#include "styles/style_editor.h"

#include <QtGui/QFontMetrics>
#include <QtGui/QPainter>

namespace Editor {
namespace {

constexpr auto kHideTimeout = crl::time(600);
constexpr auto kFadeDuration = crl::time(350);
constexpr auto kTitleFraction = 0.05f;
constexpr auto kValueFraction = 0.09f;
constexpr auto kTitleCenterFraction = 0.22f;
constexpr auto kValueCenterFraction = 0.10f;

} // namespace

EnhanceIndicator::EnhanceIndicator(not_null<Ui::RpWidget*> parent)
: RpWidget(parent)
, _hideTimer([=] { hideValue(); }) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
}

void EnhanceIndicator::setValue(int value) {
	_value = value;
	_hideTimer.callOnce(kHideTimeout);
	const auto wasShown = _shown;
	const auto from = _fade.value(wasShown ? 1. : 0.);
	if (!wasShown || _fade.animating()) {
		_shown = true;
		show();
		_fade.start(
			[=] { update(); },
			from,
			1.,
			kFadeDuration,
			anim::easeOutQuint);
	}
	update();
}

void EnhanceIndicator::hideValue() {
	_shown = false;
	_fade.start(
		[=] {
			if (!_shown && !_fade.animating()) {
				hide();
			}
			update();
		},
		_fade.value(1.),
		0.,
		kFadeDuration,
		anim::easeOutQuint);
}

void EnhanceIndicator::paintEvent(QPaintEvent *e) {
	const auto alpha = _fade.value(_shown ? 1. : 0.);
	if (alpha <= 0.) {
		return;
	}
	auto p = QPainter(this);
	p.setOpacity(alpha);
	p.setRenderHint(QPainter::TextAntialiasing);

	auto titleFont = st::semiboldFont->f;
	titleFont.setPixelSize(std::clamp(
		int(height() * kTitleFraction),
		st::photoEditorEnhanceTitleMinSize,
		st::photoEditorEnhanceTitleMaxSize));
	auto valueFont = st::semiboldFont->f;
	valueFont.setPixelSize(std::clamp(
		int(height() * kValueFraction),
		st::photoEditorEnhanceValueMinSize,
		st::photoEditorEnhanceValueMaxSize));

	const auto titleMetrics = QFontMetrics(titleFont);
	const auto valueMetrics = QFontMetrics(valueFont);
	const auto titleCenter = int(height() * kTitleCenterFraction);
	const auto valueCenter = titleCenter
		+ int(height() * kValueCenterFraction);
	const auto titleRect = QRect(
		0,
		titleCenter - titleMetrics.height() / 2,
		width(),
		titleMetrics.height());
	const auto valueRect = QRect(
		0,
		valueCenter - valueMetrics.height() / 2,
		width(),
		valueMetrics.height());
	const auto title = tr::lng_photo_editor_enhance(tr::now);
	const auto value = QString::number(_value);
	const auto shift = st::photoEditorEnhanceShadowShift;

	p.setFont(titleFont);
	p.setPen(st::photoEditorEnhanceShadow);
	p.drawText(titleRect.translated(0, shift), style::al_center, title);
	p.setPen(st::photoEditorEnhanceFg);
	p.drawText(titleRect, style::al_center, title);

	p.setFont(valueFont);
	p.setPen(st::photoEditorEnhanceShadow);
	p.drawText(valueRect.translated(0, shift), style::al_center, value);
	p.setPen(st::photoEditorEnhanceFg);
	p.drawText(valueRect, style::al_center, value);
}

} // namespace Editor
