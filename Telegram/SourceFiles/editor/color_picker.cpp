/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/color_picker.h"

#include "ui/rp_widget.h"

#include "styles/style_editor.h"

#include <QtGui/QLinearGradient>

namespace Editor {
namespace {

constexpr auto kPrecision = 1000;
constexpr auto kMinBrushSize = 0.1f;
constexpr auto kMouseSkip = 1.4;

constexpr auto kMinInnerHeight = 0.2;
constexpr auto kMaxInnerHeight = 0.8;

constexpr auto kCircleDuration = crl::time(200);

constexpr auto kMax = 1.0;

ColorPicker::OutlinedStop FindOutlinedStop(
		const QColor &color,
		const QGradientStops &stops,
		int width) {
	for (auto i = 0; i < stops.size(); i++) {
		const auto &current = stops[i];
		if (current.second == color) {
			const auto prev = ((i - 1) < 0)
				? std::nullopt
				: std::make_optional<int>(stops[i - 1].first * width);
			const auto next = ((i + 1) >= stops.size())
				? std::nullopt
				: std::make_optional<int>(stops[i + 1].first * width);
			return ColorPicker::OutlinedStop{
				.stopPos = (current.first * width),
				.prevStopPos = prev,
				.nextStopPos = next,
			};
		}
	}
	return ColorPicker::OutlinedStop();
}

QGradientStops Colors() {
	return QGradientStops{
		{ 0.00f, QColor(234, 39, 57) },
		{ 0.14f, QColor(219, 58, 210) },
		{ 0.24f, QColor(48, 81, 227) },
		{ 0.39f, QColor(73, 197, 237) },
		{ 0.49f, QColor(128, 200, 100) },
		{ 0.62f, QColor(252, 222, 101) },
		{ 0.73f, QColor(252, 150, 77) },
		{ 0.85f, QColor(0, 0, 0) },
		{ 1.00f, QColor(255, 255, 255) } };
}

QBrush GradientBrush(const QPoint &p, const QGradientStops &stops) {
	auto gradient = QLinearGradient(0, p.y(), p.x(), p.y());
	gradient.setStops(stops);
	return QBrush(std::move(gradient));
}

float RatioPrecise(float a) {
	return int(a * kPrecision) / float(kPrecision);
}

inline float64 InterpolateF(float a, float b, float64 b_ratio) {
	return a + float64(b - a) * b_ratio;
};

inline float64 InterpolationRatio(int from, int to, int result) {
	return (result - from) / float64(to - from);
};

} // namespace

ColorPicker::ColorPicker(
	not_null<Ui::RpWidget*> parent,
	const Brush &savedBrush)
: _circleColor(Qt::white)
, _width(st::photoEditorColorPickerWidth)
, _lineHeight(st::photoEditorColorPickerLineHeight)
, _colorLine(base::make_unique_q<Ui::RpWidget>(parent))
, _canvasForCircle(base::make_unique_q<Ui::RpWidget>(parent))
, _gradientStops(Colors())
, _outlinedStop(FindOutlinedStop(_circleColor, _gradientStops, _width))
, _gradientBrush(
	GradientBrush(QPoint(_width, _lineHeight / 2), _gradientStops))
, _brush(Brush{
	.sizeRatio = (savedBrush.sizeRatio
		? savedBrush.sizeRatio
		: kMinBrushSize),
	.color = (savedBrush.color.isValid()
		? savedBrush.color
		: _gradientStops.front().second),
}) {
	_colorLine->resize(_width, _lineHeight);
	_canvasForCircle->resize(
		_width + circleHeight(kMax),
		st::photoEditorColorPickerCanvasHeight);

	_canvasForCircle->setAttribute(Qt::WA_TransparentForMouseEvents);

	_down.pos = QPoint(colorToPosition(savedBrush.color), 0);

	_colorLine->paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(_colorLine);
		PainterHighQualityEnabler hq(p);

		p.setPen(Qt::NoPen);
		p.setBrush(_gradientBrush);

		const auto radius = _colorLine->height() / 2.;
		p.drawRoundedRect(_colorLine->rect(), radius, radius);
	}, _colorLine->lifetime());

	_canvasForCircle->paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(_canvasForCircle);
		paintCircle(p);
	}, _canvasForCircle->lifetime());

	_colorLine->events(
	) | rpl::start_with_next([=](not_null<QEvent*> event) {
		const auto type = event->type();
		const auto isPress = (type == QEvent::MouseButtonPress)
			|| (type == QEvent::MouseButtonDblClick);
		const auto isMove = (type == QEvent::MouseMove);
		const auto isRelease = (type == QEvent::MouseButtonRelease);
		if (!isPress && !isMove && !isRelease) {
			return;
		}
		_down.pressed = !isRelease;

		const auto progress = _circleAnimation.value(isPress ? 0. : 1.);
		if (!isMove) {
			const auto from = progress;
			const auto to = isPress ? 1. : 0.;
			_circleAnimation.stop();

			_circleAnimation.start(
				[=] { _canvasForCircle->update(); },
				from,
				to,
				kCircleDuration * std::abs(to - from),
				anim::easeOutCirc);
		}
		const auto e = static_cast<QMouseEvent*>(event.get());
		updateMousePosition(e->pos(), progress);
		if (isRelease) {
			_saveBrushRequests.fire_copy(_brush);
		}

		_canvasForCircle->update();
	}, _colorLine->lifetime());
}

void ColorPicker::updateMousePosition(const QPoint &pos, float64 progress) {
	const auto mapped = _canvasForCircle->mapFromParent(
		_colorLine->mapToParent(pos));

	const auto height = circleHeight(progress);
	const auto mappedY = int(mapped.y() - height * kMouseSkip);
	const auto bottom = _canvasForCircle->height() - circleHeight(kMax);
	const auto &skip = st::photoEditorColorPickerCircleSkip;

	_down.pos = QPoint(
		std::clamp(pos.x(), 0, _width),
		std::clamp(mappedY, 0, bottom - skip));

	// Convert Y to the brush size.
	const auto from = 0;
	const auto to = bottom - skip;

	// Don't change the brush size when we are on the color line.
	if (mappedY <= to) {
		_brush.sizeRatio = std::clamp(
			float(1. - InterpolationRatio(from, to, _down.pos.y())),
			kMinBrushSize,
			1.f);
	}
	_brush.color = positionToColor(_down.pos.x());
}

void ColorPicker::moveLine(const QPoint &position) {
	_colorLine->move(position
		- QPoint(_colorLine->width() / 2, _colorLine->height() / 2));

	_canvasForCircle->move(
		_colorLine->x() - circleHeight(kMax) / 2,
		_colorLine->y()
			+ _colorLine->height()
			+ ((circleHeight() - _colorLine->height()) / 2)
			- _canvasForCircle->height());
}

QColor ColorPicker::positionToColor(int x) const {
	const auto from = 0;
	const auto to = _width;
	const auto gradientRatio = InterpolationRatio(from, to, x);

	for (auto i = 1; i < _gradientStops.size(); i++) {
		const auto &previous = _gradientStops[i - 1];
		const auto &current = _gradientStops[i];
		const auto &fromStop = previous.first;
		const auto &toStop = current.first;
		const auto &fromColor = previous.second;
		const auto &toColor = current.second;

		if ((fromStop <= gradientRatio) && (toStop >= gradientRatio)) {
			const auto stopRatio = RatioPrecise(
				(gradientRatio - fromStop) / float64(toStop - fromStop));
			return anim::color(fromColor, toColor, stopRatio);
		}
	}
	return QColor();
}

void ColorPicker::paintCircle(Painter &p) {
	PainterHighQualityEnabler hq(p);

	p.setPen(Qt::NoPen);
	p.setBrush(_circleColor);

	const auto progress = _circleAnimation.value(_down.pressed ? 1. : 0.);
	const auto h = circleHeight(progress);
	const auto bottom = _canvasForCircle->height() - h;

	const auto circleX = _down.pos.x() + (circleHeight(kMax) - h) / 2;
	const auto circleY = _circleAnimation.animating()
		? anim::interpolate(bottom, _down.pos.y(), progress)
		: _down.pressed
		? _down.pos.y()
		: bottom;

	const auto r = QRect(circleX, circleY, h, h);
	p.drawEllipse(r);

	const auto innerH = InterpolateF(
		h * kMinInnerHeight,
		h * kMaxInnerHeight,
		_brush.sizeRatio);

	p.setBrush(_brush.color);

	const auto innerRect = QRectF(
		r.x() + (r.width() - innerH) / 2.,
		r.y() + (r.height() - innerH) / 2.,
		innerH,
		innerH);

	paintOutline(p, innerRect);
	p.drawEllipse(innerRect);
}

void ColorPicker::paintOutline(Painter &p, const QRectF &rect) {
	const auto &s = _outlinedStop;
	if (!s.stopPos) {
		return;
	}
	const auto draw = [&](float opacity) {
		p.save();
		p.setOpacity(opacity);
		p.setPen(Qt::lightGray);
		p.setPen(Qt::NoBrush);
		p.drawEllipse(rect);
		p.restore();
	};
	const auto x = _down.pos.x();
	if (s.prevStopPos && (x >= s.prevStopPos && x <= s.stopPos)) {
		const auto from = *s.prevStopPos;
		const auto to = *s.stopPos;
		const auto ratio = InterpolationRatio(from, to, x);
		if (ratio >= 0. && ratio <= 1.) {
			draw(ratio);
		}
	} else if (s.nextStopPos && (x >= s.stopPos && x <= s.nextStopPos)) {
		const auto from = *s.stopPos;
		const auto to = *s.nextStopPos;
		const auto ratio = InterpolationRatio(from, to, x);
		if (ratio >= 0. && ratio <= 1.) {
			draw(1. - ratio);
		}
	}
}

int ColorPicker::circleHeight(float64 progress) const {
	return anim::interpolate(
		st::photoEditorColorPickerCircleSize,
		st::photoEditorColorPickerCircleBigSize,
		progress);
}

void ColorPicker::setVisible(bool visible) {
	_colorLine->setVisible(visible);
	_canvasForCircle->setVisible(visible);
}

rpl::producer<Brush> ColorPicker::saveBrushRequests() const {
	return _saveBrushRequests.events_starting_with_copy(_brush);
}

int ColorPicker::colorToPosition(const QColor &color) const {
	const auto step = 1. / kPrecision;
	for (auto i = 0.; i <= 1.; i += step) {
		if (positionToColor(i * _width) == color) {
			return i * _width;
		}
	}
	return 0;
}

bool ColorPicker::preventHandleKeyPress() const {
	return _canvasForCircle->isVisible()
		&& (_circleAnimation.animating() || _down.pressed);
}

} // namespace Editor
