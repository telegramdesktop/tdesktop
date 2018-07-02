/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/continuous_sliders.h"

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
			_byWheelFinished = std::make_unique<SingleTimer>();
			_byWheelFinished->setTimeoutHandler([this] {
				if (_changeFinishedCallback) {
					_changeFinishedCallback(getCurrentValue());
				}
			});
		} else {
			_byWheelFinished.reset();
		}
	}
}

void ContinuousSlider::setValue(float64 value) {
	_value = value;
	update();
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
	const auto snapped = snap(result, 0., 1.);
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
#ifdef OS_MAC_OLD
	constexpr auto step = 120;
#else // OS_MAC_OLD
	constexpr auto step = static_cast<int>(QWheelEvent::DefaultDeltasPerStep);
#endif // OS_MAC_OLD
	constexpr auto coef = 1. / (step * 10.);

	auto deltaX = e->angleDelta().x(), deltaY = e->angleDelta().y();
	if (cPlatform() == dbipMac || cPlatform() == dbipMacOld) {
		deltaY *= -1;
	} else {
		deltaX *= -1;
	}
	auto delta = (qAbs(deltaX) > qAbs(deltaY)) ? deltaX : deltaY;
	auto finalValue = snap(_value + delta * coef, 0., 1.);
	setValue(finalValue);
	if (_changeProgressCallback) {
		_changeProgressCallback(finalValue);
	}
	_byWheelFinished->start(kByWheelFinishedTimeout);
}

void ContinuousSlider::updateDownValueFromPos(const QPoint &pos) {
	_downValue = computeValue(pos);
	update();
	if (_changeProgressCallback) {
		_changeProgressCallback(_downValue);
	}
}

void ContinuousSlider::enterEventHook(QEvent *e) {
	setOver(true);
}

void ContinuousSlider::leaveEventHook(QEvent *e) {
	setOver(false);
}

void ContinuousSlider::setOver(bool over) {
	if (_over == over) return;

	_over = over;
	auto from = _over ? 0. : 1., to = _over ? 1. : 0.;
	_a_over.start([this] { update(); }, from, to, getOverDuration());
}

FilledSlider::FilledSlider(QWidget *parent, const style::FilledSlider &st) : ContinuousSlider(parent)
, _st(st) {
}

QRect FilledSlider::getSeekRect() const {
	return QRect(0, 0, width(), height());
}

float64 FilledSlider::getOverDuration() const {
	return _st.duration;
}

void FilledSlider::paintEvent(QPaintEvent *e) {
	Painter p(this);
	PainterHighQualityEnabler hq(p);

	p.setPen(Qt::NoPen);

	auto masterOpacity = fadeOpacity();
	auto ms = getms();
	auto disabled = isDisabled();
	auto over = getCurrentOverFactor(ms);
	auto lineWidth = _st.lineWidth + ((_st.fullWidth - _st.lineWidth) * over);
	auto lineWidthRounded = qFloor(lineWidth);
	auto lineWidthPartial = lineWidth - lineWidthRounded;
	auto seekRect = getSeekRect();
	auto value = getCurrentValue();
	auto from = seekRect.x(), mid = qRound(from + value * seekRect.width()), end = from + seekRect.width();
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

QRect MediaSlider::getSeekRect() const {
	return isHorizontal()
		? QRect(_st.seekSize.width() / 2, 0, width() - _st.seekSize.width(), height())
		: QRect(0, _st.seekSize.height() / 2, width(), height() - _st.seekSize.width());
}

float64 MediaSlider::getOverDuration() const {
	return _st.duration;
}

void MediaSlider::disablePaint(bool disabled) {
	_paintDisabled = disabled;
}

void MediaSlider::paintEvent(QPaintEvent *e) {
	if (_paintDisabled) {
		return;
	}
	Painter p(this);
	PainterHighQualityEnabler hq(p);

	p.setPen(Qt::NoPen);
	p.setOpacity(fadeOpacity());

	auto horizontal = isHorizontal();
	auto ms = getms();
	auto radius = _st.width / 2;
	auto disabled = isDisabled();
	auto over = getCurrentOverFactor(ms);
	auto seekRect = getSeekRect();
	auto value = getCurrentValue();

	// invert colors and value for vertical
	if (!horizontal) value = 1. - value;

	auto markerFrom = (horizontal ? seekRect.x() : seekRect.y());
	auto markerLength = (horizontal ? seekRect.width() : seekRect.height());
	auto from = _alwaysDisplayMarker ? 0 : markerFrom;
	auto length = _alwaysDisplayMarker ? (horizontal ? width() : height()) : markerLength;
	auto mid = qRound(from + value * length);
	auto end = from + length;
	auto activeFg = disabled ? _st.activeFgDisabled : anim::brush(_st.activeFg, _st.activeFgOver, over);
	auto inactiveFg = disabled ? _st.inactiveFgDisabled : anim::brush(_st.inactiveFg, _st.inactiveFgOver, over);
	if (mid > from) {
		auto fromClipRect = horizontal ? QRect(0, 0, mid, height()) : QRect(0, 0, width(), mid);
		auto fromRect = horizontal
			? QRect(from, (height() - _st.width) / 2, mid + radius - from, _st.width)
			: QRect((width() - _st.width) / 2, from, _st.width, mid + radius - from);
		p.setClipRect(fromClipRect);
		p.setBrush(horizontal ? activeFg : inactiveFg);
		p.drawRoundedRect(fromRect, radius, radius);
	}
	if (end > mid) {
		auto endClipRect = horizontal ? QRect(mid, 0, width() - mid, height()) : QRect(0, mid, width(), height() - mid);
		auto endRect = horizontal
			? QRect(mid - radius, (height() - _st.width) / 2, end - (mid - radius), _st.width)
			: QRect((width() - _st.width) / 2, mid - radius, _st.width, end - (mid - radius));
		p.setClipRect(endClipRect);
		p.setBrush(horizontal ? inactiveFg : activeFg);
		p.drawRoundedRect(endRect, radius, radius);
	}
	auto markerSizeRatio = disabled ? 0. : (_alwaysDisplayMarker ? 1. : over);
	if (markerSizeRatio > 0) {
		auto position = qRound(markerFrom + value * markerLength) - (horizontal ? seekRect.x() : seekRect.y());
		auto seekButton = horizontal
			? QRect(position, (height() - _st.seekSize.height()) / 2, _st.seekSize.width(), _st.seekSize.height())
			: QRect((width() - _st.seekSize.width()) / 2, position, _st.seekSize.width(), _st.seekSize.height());
		auto size = horizontal ? _st.seekSize.width() : _st.seekSize.height();
		auto remove = static_cast<int>(((1. - markerSizeRatio) * size) / 2.);
		if (remove * 2 < size) {
			p.setClipRect(rect());
			p.setBrush(activeFg);
			p.drawEllipse(seekButton.marginsRemoved(QMargins(remove, remove, remove, remove)));
		}
	}
}

} // namespace Ui
