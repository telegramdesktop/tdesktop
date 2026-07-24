/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/skeleton_animation.h"

#include "ui/painter.h"
#include "ui/widgets/labels.h"
#include "styles/style_widgets.h"

namespace Ui {
namespace {

constexpr auto kSlideDuration = crl::time(1000);
constexpr auto kWaitDuration = crl::time(1000);
constexpr auto kFullDuration = kSlideDuration + kWaitDuration;
constexpr auto kBaseAlpha = 0.5;
constexpr auto kGradientAlpha = 0.2;

[[nodiscard]] QColor ColorWithAlpha(
		const style::color &color,
		float64 alpha) {
	auto result = color->c;
	result.setAlphaF(result.alphaF() * alpha);
	return result;
}

} // namespace

SkeletonAnimation::SkeletonAnimation(not_null<FlatLabel*> label)
: _label(label)
, _animation([=] { _label->update(); }) {
	base::install_event_filter(_label, _label, [=](not_null<QEvent*> e) {
		return (_active && e->type() == QEvent::Paint)
			? paint()
			: base::EventFilterResult::Continue;
	});
	_label->sizeValue(
	) | rpl::on_next([=](QSize) {
		if (_active) {
			recompute();
		}
	}, _lifetime);
}

void SkeletonAnimation::start() {
	_active = true;
	recompute();
	_animation.start();
	_label->update();
}

void SkeletonAnimation::stop() {
	_active = false;
	_animation.stop();
	_label->update();
}

bool SkeletonAnimation::active() const {
	return _active;
}

void SkeletonAnimation::recompute() {
	_lineWidths = _label->countLineWidths();
}

base::EventFilterResult SkeletonAnimation::paint() {
	if (_lineWidths.empty()) {
		return base::EventFilterResult::Cancel;
	}
	auto p = QPainter(_label);
	PainterHighQualityEnabler hq(p);

	const auto &st = _label->st();
	const auto lineHeight = st.style.lineHeight
		? st.style.lineHeight
		: st.style.font->height;
	const auto thickness = st.style.font->height / 2;
	const auto radius = thickness / 2.;
	const auto left = st.margin.left();
	const auto textWidth = _label->width()
		- st.margin.left()
		- st.margin.right();
	const auto baseColor = ColorWithAlpha(
		st::windowSubTextFg,
		kBaseAlpha);

	const auto now = crl::now();
	const auto period = now % kFullDuration;
	const auto gradientActive = (period < kSlideDuration);

	p.setPen(Qt::NoPen);
	if (gradientActive) {
		const auto progress = period
			/ float64(kSlideDuration);
		const auto gradientWidth = textWidth;
		const auto gradientStart = anim::interpolate(
			left - gradientWidth,
			left + textWidth,
			progress);
		auto gradient = QLinearGradient(
			gradientStart,
			0,
			gradientStart + gradientWidth,
			0);
		const auto centerColor = ColorWithAlpha(
			st::windowSubTextFg,
			kGradientAlpha);
		gradient.setStops({
			{ 0., baseColor },
			{ 0.5, centerColor },
			{ 1., baseColor },
		});
		p.setBrush(gradient);
	} else {
		p.setBrush(baseColor);
	}
	auto lineTop = st.margin.top();
	for (const auto lineWidth : _lineWidths) {
		if (lineWidth <= 0) {
			lineTop += lineHeight;
			continue;
		}
		const auto rect = QRectF(
			left,
			lineTop + (lineHeight - thickness) / 2.,
			lineWidth,
			thickness);
		p.drawRoundedRect(rect, radius, radius);
		lineTop += lineHeight;
	}
	return base::EventFilterResult::Cancel;
}

} // namespace Ui
