/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/color_picker.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/color_contrast.h"
#include "styles/style_editor.h"

#include <QtGui/QLinearGradient>
#include <QtCore/QMarginsF>

#include <algorithm>

namespace Editor {
namespace {

constexpr auto kPrecision = 1000;
constexpr auto kMinBrushSize = 0.1f;
constexpr auto kMouseSkip = 1.4;

constexpr auto kMinInnerHeight = 0.2;
constexpr auto kMaxInnerHeight = 0.8;

constexpr auto kCircleDuration = crl::time(200);

constexpr auto kMax = 1.0;

constexpr auto kHighContrastBorderRatio = 0.12;

[[nodiscard]] QColor HighContrastBorderColor(const QColor &color) {
	const auto black = QColor(Qt::black);
	const auto white = QColor(Qt::white);
	return (Ui::CountContrast(color, black)
		>= Ui::CountContrast(color, white))
		? black
		: white;
}

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
})
, _highContrastMarker(Core::App().settings().photoEditorHighContrastMarker()) {
	_colorLine->resize(_width, _lineHeight);
	_canvasForCircle->resize(
		_width + circleHeight(kMax),
		st::photoEditorColorPickerCanvasHeight);

	_canvasForCircle->setAttribute(Qt::WA_TransparentForMouseEvents);

	_down.pos = QPoint(colorToPosition(savedBrush.color), 0);

	_colorLine->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(_colorLine);
		PainterHighQualityEnabler hq(p);

		p.setPen(Qt::NoPen);
		p.setBrush(_gradientBrush);

		const auto radius = _colorLine->height() / 2.;
		p.drawRoundedRect(_colorLine->rect(), radius, radius);
	}, _colorLine->lifetime());

	_canvasForCircle->paintRequest(
	) | rpl::start_with_next([=] {
		auto p = QPainter(_canvasForCircle);
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

	Core::App().settings().photoEditorHighContrastMarkerValue(
	) | rpl::start_with_next([=](bool enabled) {
		if (_highContrastMarker != enabled) {
			_highContrastMarker = enabled;
			_canvasForCircle->update();
		}
	}, _canvasForCircle->lifetime());
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

	// Don't change the brush size when we are on the color line.
	const auto maxY = bottom - skip;
	if (mappedY <= maxY) {
		const auto ratio = 1. - InterpolationRatio(0, maxY, _down.pos.y());
		_brush.sizeRatio = std::clamp(float(ratio), kMinBrushSize, 1.f);
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
	const auto gradientRatio = InterpolationRatio(0, _width, x);

	for (auto i = 1; i < _gradientStops.size(); ++i) {
		const auto &previous = _gradientStops[i - 1];
		const auto &current = _gradientStops[i];
		const auto fromStop = previous.first;
		const auto toStop = current.first;

		if (fromStop <= gradientRatio && toStop >= gradientRatio) {
			const auto stopRatio = RatioPrecise(
				(gradientRatio - fromStop) / float64(toStop - fromStop));
			return anim::color(previous.second, current.second, stopRatio);
		}
	}
	return QColor();
}

void ColorPicker::paintCircle(QPainter &p) {
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

	const auto r = QRectF(circleX, circleY, h, h);
	p.drawEllipse(r);

	const auto innerH = InterpolateF(
		h * kMinInnerHeight,
		h * kMaxInnerHeight,
		_brush.sizeRatio);

	const auto innerRect = QRectF(
		r.x() + (r.width() - innerH) / 2.,
		r.y() + (r.height() - innerH) / 2.,
		innerH,
		innerH);

	paintOutline(p, innerRect);

	if (_highContrastMarker) {
		const auto borderColor = HighContrastBorderColor(_brush.color);
		const auto available = (r.width() - innerRect.width()) / 2.;
		const auto desired = innerRect.width() * kHighContrastBorderRatio;
		const auto border = std::clamp(desired, 1., available);
		if (border > 0.) {
			const auto borderRect = innerRect.marginsAdded(QMarginsF(
				border,
				border,
				border,
				border));
			p.save();
			p.setPen(Qt::NoPen);
			p.setBrush(borderColor);
			p.drawEllipse(borderRect);
			p.restore();
		}
	}

	p.setPen(Qt::NoPen);
	p.setBrush(_brush.color);
	p.drawEllipse(innerRect);
}

void ColorPicker::paintOutline(QPainter &p, const QRectF &rect) {
	const auto &s = _outlinedStop;
	if (!s.stopPos) {
		return;
	}
	const auto draw = [&](float opacity) {
		p.save();
		p.setOpacity(opacity);
		p.setPen(Qt::lightGray);
		p.setBrush(Qt::NoBrush);
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
	constexpr auto step = 1. / kPrecision;
	for (auto i = 0.; i <= 1.; i += step) {
		if (positionToColor(int(i * _width)) == color) {
			return int(i * _width);
		}
	}
	return 0;
}

bool ColorPicker::preventHandleKeyPress() const {
	return _canvasForCircle->isVisible()
		&& (_circleAnimation.animating() || _down.pressed);
}

} // namespace Editor
