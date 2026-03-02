/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/middle_click_autoscroll.h"

#include "styles/style_widgets.h"
#include "ui/effects/animation_value.h"
#include "ui/rect.h"

namespace Ui {
namespace {

constexpr auto kDelay = 15;

[[nodiscard]] int Deadzone() {
	return std::max(1, int(std::lround(style::ConvertFloatScale(6.))));
}

[[nodiscard]] float64 SpeedScale() {
	return style::ConvertFloatScale(30.);
}

[[nodiscard]] float64 MaxSpeed() {
	return style::ConvertFloatScale(3600.);
}

[[nodiscard]] QImage PaintCursorImage(
		int size,
		int factor,
		bool drawOuter,
		bool drawTopArrow,
		bool drawBottomArrow,
		const QColor &inner,
		const QColor &indicator,
		const QColor &topArrow,
		const QColor &bottomArrow) {
	auto image = QImage(
		QSize(size, size) * factor,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(factor);
	image.fill(Qt::transparent);
	auto p = QPainter(&image);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);

	const auto outerRect = QRectF(QPointF(0.5, 0.5), Size(size - 1.));
	if (drawOuter) {
		const auto outerStroke = std::max(1., style::ConvertFloatScale(1.5));
		auto pen = QPen(indicator);
		pen.setWidthF(outerStroke);
		p.setPen(pen);
		p.setBrush(inner);
		p.drawEllipse(outerRect.marginsRemoved(Margins(outerStroke * 0.5)));
	}
	p.setPen(Qt::NoPen);
	const auto centerSkip = style::ConvertFloatScale(10.5);
	const auto centerRect = outerRect - Margins(centerSkip);
	p.setBrush(indicator);
	p.drawEllipse(centerRect);

	const auto middle = size * 0.5;
	const auto arrowGap = style::ConvertFloatScale(5.);
	const auto headHalfWidth = style::ConvertFloatScale(4.5);
	const auto headHeight = style::ConvertFloatScale(3.5);
	const auto stroke = std::max(1., style::ConvertFloatScale(2.));
	const auto drawArrow = [&](bool up, const QColor &color) {
		auto pen = QPen(color);
		pen.setWidthF(stroke);
		pen.setCapStyle(Qt::RoundCap);
		pen.setJoinStyle(Qt::RoundJoin);
		p.setPen(pen);
		const auto dir = up ? -1. : 1.;
		const auto tipY = middle + dir * (arrowGap + headHeight);
		const auto neckY = tipY - dir * headHeight;
		p.drawLine(
			QPointF(middle, tipY),
			QPointF(middle - headHalfWidth, neckY));
		p.drawLine(
			QPointF(middle, tipY),
			QPointF(middle + headHalfWidth, neckY));
	};
	if (drawTopArrow) {
		drawArrow(true, topArrow);
	}
	if (drawBottomArrow) {
		drawArrow(false, bottomArrow);
	}
	return image;
}

} // namespace

MiddleClickAutoscroll::MiddleClickAutoscroll(
	Fn<void(int)> scrollBy,
	Fn<void(const QCursor &)> applyCursor,
	Fn<void()> restoreCursor,
	Fn<bool()> shouldContinue)
: _scrollBy(std::move(scrollBy))
, _applyCursor(std::move(applyCursor))
, _restoreCursor(std::move(restoreCursor))
, _shouldContinue(std::move(shouldContinue))
, _timer([=] { onTimer(); }) {
}

void MiddleClickAutoscroll::start(const QPoint &globalPosition) {
	if (_active) {
		stop();
	}
	_active = true;
	_startPosition = globalPosition;
	_time = crl::now();
	updateCursor(QCursor::pos().y() - _startPosition.y());
	_timer.callEach(kDelay);
}

void MiddleClickAutoscroll::stop() {
	if (!_active) {
		return;
	}
	_active = false;
	_timer.cancel();
	if (_restoreCursor) {
		_restoreCursor();
	}
}

void MiddleClickAutoscroll::updateCursor(int delta) {
	if (!_applyCursor) {
		return;
	}
	const auto mode = (std::abs(delta) <= Deadzone())
		? CursorMode::Neutral
		: (delta < 0)
		? CursorMode::Up
		: CursorMode::Down;
	_applyCursor(makeCursor(mode));
}

QCursor MiddleClickAutoscroll::makeCursor(CursorMode mode) {
	struct CachedCursor {
		CursorMode mode = CursorMode::Neutral;
		int size = 0;
		int factor = 0;
		QColor inner;
		QColor indicator;
		QCursor cursor;
	};
	const auto size = std::max(
		1,
		int(std::lround(style::ConvertFloatScale(27.))));
	const auto factor = style::DevicePixelRatio();
	const auto inner = anim::with_alpha(st::windowBg->c, (240. / 255.));
	const auto active = anim::with_alpha(st::windowFg->c, (210. / 255.));
	const auto passive = anim::with_alpha(
		st::windowSubTextFg->c,
		(210. / 255.));
	const auto neutral = (mode == CursorMode::Neutral);
	const auto indicator = neutral ? passive : active;
	static auto cache = std::vector<CachedCursor>();
	if (const auto i = ranges::find_if(
			cache,
			[&](const CachedCursor &entry) {
				return (entry.mode == mode)
					&& (entry.size == size)
					&& (entry.factor == factor)
					&& (entry.inner == inner)
					&& (entry.indicator == indicator);
			});
		i != cache.end()) {
		return i->cursor;
	}
	const auto image = PaintCursorImage(
		size,
		factor,
		neutral,
		mode != CursorMode::Down,
		mode != CursorMode::Up,
		inner,
		indicator,
		indicator,
		indicator);
	const auto hot = int(std::floor(size * 0.5));
	auto cursor = QCursor(QPixmap::fromImage(image), hot, hot);
	cache.push_back(CachedCursor{
		.mode = mode,
		.size = size,
		.factor = factor,
		.inner = inner,
		.indicator = indicator,
		.cursor = cursor,
	});
	return cursor;
}

void MiddleClickAutoscroll::onTimer() {
	if (!_active) {
		return;
	} else if (_shouldContinue && !_shouldContinue()) {
		stop();
		return;
	}
	const auto now = crl::now();
	const auto elapsed = std::max(now - _time, crl::time(1));
	_time = now;
	const auto delta = QCursor::pos().y() - _startPosition.y();
	updateCursor(delta);
	const auto absolute = std::abs(delta) - Deadzone();
	if (absolute <= 0) {
		return;
	}
	const auto speed = std::min(
		absolute * SpeedScale(),
		MaxSpeed());
	auto scroll = int(std::lround((speed * elapsed) / 1000.));
	if (scroll <= 0) {
		scroll = 1;
	}
	if (delta < 0) {
		scroll = -scroll;
	}
	_scrollBy(scroll);
}

} // namespace Ui
