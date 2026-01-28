/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/continuous_sliders.h"

#include "ui/painter.h"
#include "ui/rect.h"
#include "base/timer.h"
#include "base/platform/base_platform_info.h"
#include "styles/style_widgets.h"

namespace Ui {
namespace {

constexpr auto kByWheelFinishedTimeout = 1000;

} // namespace

ContinuousSlider::ContinuousSlider(QWidget *parent) : RpWidget(parent) {
	setCursor(style::cur_pointer);
}

void ContinuousSlider::setDisabled(bool disabled) {
	if (_disabled != disabled) {
		_disabled = disabled;
		setCursor(_disabled ? style::cur_default : style::cur_pointer);
		update();
	}
}

void ContinuousSlider::setMoveByWheel(bool move) {
	if (move != moveByWheel()) {
		if (move) {
			_byWheelFinished = std::make_unique<base::Timer>([=] {
				if (_changeFinishedCallback) {
					_changeFinishedCallback(getCurrentValue());
				}
			});
		} else {
			_byWheelFinished = nullptr;
		}
	}
}

QRect ContinuousSlider::getSeekRect() const {
	const auto decrease = getSeekDecreaseSize();
	return isHorizontal()
		? QRect(decrease.width() / 2, 0, width() - decrease.width(), height())
		: QRect(0, decrease.height() / 2, width(), height() - decrease.width());
}

float64 ContinuousSlider::value() const {
	return getCurrentValue();
}

void ContinuousSlider::setValue(float64 value) {
	setValue(value, -1);
}

void ContinuousSlider::setValue(float64 value, float64 receivedTill) {
	if (_value != value || _receivedTill != receivedTill) {
		_value = value;
		_receivedTill = receivedTill;
		update();
	}
}

void ContinuousSlider::setFadeOpacity(float64 opacity) {
	_fadeOpacity = opacity;
	update();
}

void ContinuousSlider::mouseMoveEvent(QMouseEvent *e) {
	if (_mouseDown) {
		updateDownValueFromPos(e->pos());
	}
}

float64 ContinuousSlider::computeValue(const QPoint &pos) const {
	const auto seekRect = myrtlrect(getSeekRect());
	const auto result = isHorizontal() ?
		(pos.x() - seekRect.x()) / float64(seekRect.width()) :
		(1. - (pos.y() - seekRect.y()) / float64(seekRect.height()));
	const auto snapped = std::clamp(result, 0., 1.);
	return _adjustCallback ? _adjustCallback(snapped) : snapped;
}

void ContinuousSlider::mousePressEvent(QMouseEvent *e) {
	_mouseDown = true;
	_downValue = computeValue(e->pos());
	update();
	if (_changeProgressCallback) {
		_changeProgressCallback(_downValue);
	}
}

void ContinuousSlider::mouseReleaseEvent(QMouseEvent *e) {
	if (_mouseDown) {
		_mouseDown = false;
		if (_changeFinishedCallback) {
			_changeFinishedCallback(_downValue);
		}
		_value = _downValue;
		update();
	}
}

void ContinuousSlider::wheelEvent(QWheelEvent *e) {
	if (_mouseDown || !moveByWheel()) {
		return;
	}
	constexpr auto step = static_cast<int>(QWheelEvent::DefaultDeltasPerStep);
	constexpr auto coef = 1. / (step * 10.);

	auto deltaX = e->angleDelta().x(), deltaY = e->angleDelta().y();
	if (Platform::IsMac()) {
		deltaY *= -1;
	} else {
		deltaX *= -1;
	}
	auto delta = (qAbs(deltaX) > qAbs(deltaY)) ? deltaX : deltaY;
	auto finalValue = std::clamp(_value + delta * coef, 0., 1.);
	setValue(finalValue);
	if (_changeProgressCallback) {
		_changeProgressCallback(finalValue);
	}
	_byWheelFinished->callOnce(kByWheelFinishedTimeout);
}

void ContinuousSlider::keyPressEvent(QKeyEvent *e) {
	const auto changeBy = [&](float64 step) {
		Expects(step != 0.);

		auto steps = 0;
		while (true) {
			++steps;
			auto result = _value + (steps * step);
			const auto stopping = (result <= 0.) || (result >= 1.);
			if (_adjustCallback) {
				result = _adjustCallback(result);
			}
			result = std::clamp(result, 0., 1.);
			if (result != _value || stopping) {
				return result;
			}
		}
	};

	const auto newValue = [&] {
		constexpr auto kSmallStep = 0.01;
		constexpr auto kLargeStep = 0.10;
		switch (e->key()) {
		case Qt::Key_Right:
		case Qt::Key_Up: return changeBy(kSmallStep);
		case Qt::Key_Left:
		case Qt::Key_Down: return changeBy(-kSmallStep);
		case Qt::Key_PageUp: return changeBy(kLargeStep);
		case Qt::Key_PageDown: return changeBy(-kLargeStep);
		case Qt::Key_Home: return changeBy(-1.);
		case Qt::Key_End: return changeBy(1.);
		default: e->ignore();
		}
		return _value;
	}();

	if (_value == newValue) {
		return;
	}
	setValue(newValue);
	if (_changeProgressCallback) {
		_changeProgressCallback(_value);
	}
	if (_changeFinishedCallback) {
		_changeFinishedCallback(_value);
	}
	accessibilityValueChanged();
}

void ContinuousSlider::updateDownValueFromPos(const QPoint &pos) {
	_downValue = computeValue(pos);
	update();
	if (_changeProgressCallback) {
		_changeProgressCallback(_downValue);
	}
}

void ContinuousSlider::enterEventHook(QEnterEvent *e) {
	setOver(true);
}

void ContinuousSlider::leaveEventHook(QEvent *e) {
	setOver(false);
}

void ContinuousSlider::setOver(bool over) {
	if (_over == over) return;

	_over = over;
	auto from = _over ? 0. : 1., to = _over ? 1. : 0.;
	_overAnimation.start([=] { update(); }, from, to, getOverDuration());
}

FilledSlider::FilledSlider(QWidget *parent, const style::FilledSlider &st) : ContinuousSlider(parent)
, _st(st) {
}

QSize FilledSlider::getSeekDecreaseSize() const {
	return QSize(0, 0);
}

float64 FilledSlider::getOverDuration() const {
	return _st.duration;
}

void FilledSlider::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	PainterHighQualityEnabler hq(p);

	p.setPen(Qt::NoPen);

	const auto masterOpacity = fadeOpacity();
	const auto disabled = isDisabled();
	const auto over = getCurrentOverFactor();
	const auto lineWidth = _st.lineWidth + ((_st.fullWidth - _st.lineWidth) * over);
	const auto lineWidthRounded = std::floor(lineWidth);
	const auto lineWidthPartial = lineWidth - lineWidthRounded;
	const auto seekRect = getSeekRect();
	const auto value = getCurrentValue();
	const auto from = seekRect.x();
	const auto mid = qRound(from + value * seekRect.width());
	const auto end = from + seekRect.width();
	if (mid > from) {
		p.setOpacity(masterOpacity);
		p.fillRect(from, height() - lineWidthRounded, (mid - from), lineWidthRounded, disabled ? _st.disabledFg : _st.activeFg);
		if (lineWidthPartial > 0.01) {
			p.setOpacity(masterOpacity * lineWidthPartial);
			p.fillRect(from, height() - lineWidthRounded - 1, (mid - from), 1, disabled ? _st.disabledFg : _st.activeFg);
		}
	}
	if (end > mid && over > 0) {
		p.setOpacity(masterOpacity * over);
		p.fillRect(mid, height() - lineWidthRounded, (end - mid), lineWidthRounded, _st.inactiveFg);
		if (lineWidthPartial > 0.01) {
			p.setOpacity(masterOpacity * over * lineWidthPartial);
			p.fillRect(mid, height() - lineWidthRounded - 1, (end - mid), 1, _st.inactiveFg);
		}
	}
}

MediaSlider::MediaSlider(QWidget *parent, const style::MediaSlider &st) : ContinuousSlider(parent)
, _st(st) {
}

QSize MediaSlider::getSeekDecreaseSize() const {
	return _alwaysDisplayMarker ? _st.seekSize : QSize();
}

float64 MediaSlider::getOverDuration() const {
	return _st.duration;
}

void MediaSlider::disablePaint(bool disabled) {
	_paintDisabled = disabled;
}

void MediaSlider::addDivider(float64 atValue, const QSize &size) {
	_dividers.push_back(Divider{ atValue, size });
}

void MediaSlider::setColorOverrides(ColorOverrides overrides) {
	_overrides = std::move(overrides);
	update();
}

void MediaSlider::paintEvent(QPaintEvent *e) {
	if (_paintDisabled) {
		return;
	}
	auto p = QPainter(this);
	PainterHighQualityEnabler hq(p);

	p.setPen(Qt::NoPen);
	p.setOpacity(fadeOpacity());

	const auto horizontal = isHorizontal();
	const auto borderWidth = _st.borderWidth;
	const auto borderHalf = borderWidth / 2;
	const auto radius = _st.width / 2;
	const auto disabled = isDisabled();
	const auto over = getCurrentOverFactor();
	const auto seekRect = getSeekRect();

	// invert colors and value for vertical
	const auto value = horizontal
		? getCurrentValue()
		: (1. - getCurrentValue());

	// receivedTill is not supported for vertical
	const auto receivedTill = horizontal
		? getCurrentReceivedTill()
		: value;

	const auto markerFrom = (horizontal ? seekRect.x() : seekRect.y());
	const auto markerLength = horizontal
		? seekRect.width()
		: seekRect.height();
	const auto from = 0;
	const auto length = (horizontal ? width() : height());
	const auto alwaysSeekSize = horizontal
		? _st.seekSize.width()
		: _st.seekSize.height();
	const auto mid = _alwaysDisplayMarker
		? qRound(from
			+ (alwaysSeekSize / 2.)
			+ value * (length - alwaysSeekSize))
		: qRound(from + value * length);
	const auto till = horizontal
		? std::max(mid, qRound(from + receivedTill * length))
		: mid;
	const auto end = from + length;
	const auto activeFg = disabled
		? _st.activeFgDisabled
		: _overrides.activeFg
		? QBrush(*_overrides.activeFg)
		: anim::brush(_st.activeFg, _st.activeFgOver, over);
	const auto receivedTillFg = _st.receivedTillFg;
	const auto inactiveFg = disabled
		? _st.inactiveFgDisabled
		: _overrides.inactiveFg
		? QBrush(*_overrides.inactiveFg)
		: anim::brush(_st.inactiveFg, _st.inactiveFgOver, over);
	const auto borderFg = _st.borderFg;
	if (mid > from) {
		const auto fromClipRect = horizontal
			? QRect(0, 0, mid, height())
			: QRect(0, 0, width(), mid);
		const auto till = std::min(mid + radius, end);
		const auto fromRect = horizontal
			? QRect(
				from + borderHalf,
				(height() - _st.width) / 2 + borderHalf,
				till - from - borderWidth,
				_st.width - borderWidth)
			: QRect(
				(width() - _st.width) / 2 + borderHalf,
				from + borderHalf,
				_st.width - borderWidth,
				till - from - borderWidth);
		p.setClipRect(fromClipRect);
		if (borderWidth > 0) {
			const auto borderPen = _overrides.activeBorder
				? QPen(*_overrides.activeBorder, borderWidth)
				: QPen(borderFg, borderWidth);
			const auto bgBrush = _overrides.activeBg
				? QBrush(*_overrides.activeBg)
				: (horizontal ? borderFg : inactiveFg);
			p.setPen(borderPen);
			p.setBrush(bgBrush);
		} else {
			p.setPen(Qt::NoPen);
			p.setBrush(horizontal ? activeFg : inactiveFg);
		}
		p.drawRoundedRect(fromRect, radius, radius);
	}
	if (till > mid) {
		Assert(horizontal);
		auto clipRect = QRect(mid, 0, till - mid, height());
		const auto left = std::max(mid - radius, from);
		const auto right = std::min(till + radius, end);
		const auto rect = QRect(
			left,
			(height() - _st.width) / 2,
			right - left,
			_st.width);
		p.setClipRect(clipRect);
		p.setBrush(receivedTillFg);
		p.drawRoundedRect(rect, radius, radius);
	}
	if (end > till) {
		const auto endClipRect = horizontal
			? QRect(till, 0, width() - till, height())
			: QRect(0, till, width(), height() - till);
		const auto begin = std::max(till - radius, from);
		const auto endRect = horizontal
			? QRect(
				begin + borderHalf,
				(height() - _st.width) / 2 + borderHalf,
				end - begin - borderWidth,
				_st.width - borderWidth)
			: QRect(
				(width() - _st.width) / 2 + borderHalf,
				begin + borderHalf,
				_st.width - borderWidth,
				end - begin - borderWidth);
		p.setClipRect(endClipRect);
		if (borderWidth > 0) {
			const auto endBorderPen = _overrides.inactiveBorder
				? QPen(*_overrides.inactiveBorder, borderWidth)
				: QPen(borderFg, borderWidth);
			p.setPen(endBorderPen);
		} else {
			p.setPen(Qt::NoPen);
		}
		p.setBrush(horizontal ? inactiveFg : activeFg);
		p.drawRoundedRect(endRect, radius, radius);
	}
	if (!_dividers.empty()) {
		p.setClipRect(rect());
		for (const auto &divider : _dividers) {
			const auto dividerValue = horizontal
				? divider.atValue
				: (1. - divider.atValue);
			const auto dividerMid = base::SafeRound(from
				+ dividerValue * length);
			const auto &size = divider.size;
			const auto rect = horizontal
				? QRect(
					dividerMid - size.width() / 2,
					(height() - size.height()) / 2,
					size.width(),
					size.height())
				: QRect(
					(width() - size.height()) / 2,
					dividerMid - size.width() / 2,
					size.height(),
					size.width());
			p.setBrush(((value < dividerValue) == horizontal)
				? inactiveFg
				: activeFg);
			const auto dividerRadius = size.width() / 2.;
			p.drawRoundedRect(rect, dividerRadius, dividerRadius);
		}
	}
	const auto markerSizeRatio = disabled
		? 0.
		: (_alwaysDisplayMarker ? 1. : over);
	if (markerSizeRatio > 0) {
		const auto position = qRound(markerFrom + value * markerLength)
			- (horizontal
				? (_st.seekSize.width() / 2)
				: (_st.seekSize.height() / 2));
		const auto seekButton = horizontal
			? QRect(
				position,
				(height() - _st.seekSize.height()) / 2,
				_st.seekSize.width(),
				_st.seekSize.height())
			: QRect(
				(width() - _st.seekSize.width()) / 2,
				position,
				_st.seekSize.width(),
				_st.seekSize.height());
		const auto size = horizontal
			? _st.seekSize.width()
			: _st.seekSize.height();
		const auto remove = static_cast<int>(
			((1. - markerSizeRatio) * size) / 2.);
		if (remove * 2 < size) {
			p.setClipRect(rect());
			const auto seekFg = _overrides.seekFg
				? QBrush(*_overrides.seekFg)
				: activeFg;
			if (borderWidth > 0) {
				const auto seekBorderPen = _overrides.seekBorder
					? QPen(*_overrides.seekBorder, borderWidth)
					: QPen(borderFg, borderWidth);
				p.setPen(seekBorderPen);
				p.setBrush(seekFg);
			} else {
				p.setPen(Qt::NoPen);
				p.setBrush(seekFg);
			}
			const auto xshift = horizontal
				? std::max(
					seekButton.x() + seekButton.width() - remove - width(),
					0) + std::min(seekButton.x() + remove, 0)
				: 0;
			const auto yshift = horizontal
				? 0
				: std::max(
					seekButton.y() + seekButton.height() - remove - height(),
					0) + std::min(seekButton.y() + remove, 0);
			auto ellipseRect = (seekButton - Margins(remove)).translated(
				-xshift,
				-yshift);
			if (borderWidth > 0) {
				ellipseRect -= Margins(borderHalf);
			}
			p.drawEllipse(ellipseRect);
		}
	}
}

} // namespace Ui
