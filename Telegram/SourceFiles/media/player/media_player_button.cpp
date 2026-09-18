/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/player/media_player_button.h"

#include "media/media_common.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "styles/style_media_player.h"
#include "styles/style_media_view.h"

#include <QtGui/QPainterPathStroker>

namespace Media::Player {
namespace {

using Quad = std::array<QPointF, 4>;

struct Shape {
	Quad left;
	Quad right;
	float64 radius = 0.;
};

[[nodiscard]] QString SpeedText(float64 speed) {
	return QString::number(base::SafeRound(speed * 10) / 10.) + 'X';
}

[[nodiscard]] QPointF Normalized(QPointF value) {
	const auto length = std::hypot(value.x(), value.y());
	return length ? (value / length) : value;
}

[[nodiscard]] QPointF InsetCorner(
		QPointF previous,
		QPointF corner,
		QPointF next,
		float64 radius) {
	const auto in = Normalized(corner - previous);
	const auto out = Normalized(next - corner);
	const auto cosine = (in.x() * out.x()) + (in.y() * out.y());
	const auto sine = std::sqrt(std::max((1. + cosine) / 2., 0.));
	return corner + Normalized(out - in) * (radius / sine);
}

[[nodiscard]] Shape PlayShape(const style::MediaPlayerPlayIcon &st) {
	const auto radius = style::ConvertScaleExact(st.playRadius);
	const auto left = 0. + st.playPosition.x();
	const auto top = 0. + st.playPosition.y();
	const auto right = left + st.playSize.width();
	const auto bottom = top + st.playSize.height();
	const auto first = QPointF(left, top);
	const auto second = QPointF(right, (top + bottom) / 2.);
	const auto third = QPointF(left, bottom);
	const auto one = InsetCorner(third, first, second, radius);
	const auto two = InsetCorner(first, second, third, radius);
	const auto three = InsetCorner(second, third, first, radius);
	const auto upper = (one + two) / 2.;
	const auto lower = (three + two) / 2.;
	return {
		{ one, upper, lower, three },
		{ upper, two, two, lower },
		radius,
	};
}

[[nodiscard]] Shape PauseShape(const style::MediaPlayerPlayIcon &st) {
	const auto radius = style::ConvertScaleExact(st.pauseRadius);
	const auto top = st.pausePosition.y() + radius;
	const auto bottom = st.pausePosition.y()
		+ st.pauseSize.height()
		- radius;
	const auto bar = [&](float64 left) {
		const auto right = left + st.pauseBarWidth - 2 * radius;
		return Quad{
			QPointF(left, top),
			QPointF(right, top),
			QPointF(right, bottom),
			QPointF(left, bottom),
		};
	};
	const auto left = st.pausePosition.x() + radius;
	const auto skip = st.pauseSize.width() - st.pauseBarWidth;
	return { bar(left), bar(left + skip), radius };
}

[[nodiscard]] Shape CancelShape(const style::MediaPlayerPlayIcon &st) {
	const auto radius = style::ConvertScaleExact(st.cancelRadius);
	const auto left = st.cancelPosition.x() + radius;
	const auto top = st.cancelPosition.y() + radius;
	const auto right = st.cancelPosition.x()
		+ st.cancelSize.width()
		- radius;
	const auto bottom = st.cancelPosition.y()
		+ st.cancelSize.height()
		- radius;
	const auto topLeft = QPointF(left, top);
	const auto topRight = QPointF(right, top);
	const auto bottomLeft = QPointF(left, bottom);
	const auto bottomRight = QPointF(right, bottom);
	return {
		{ topLeft, topLeft, bottomRight, bottomRight },
		{ topRight, topRight, bottomLeft, bottomLeft },
		radius,
	};
}

[[nodiscard]] Shape Interpolate(
		const Shape &from,
		const Shape &to,
		float64 ratio) {
	auto result = Shape();
	for (auto i = 0; i != 4; ++i) {
		result.left[i] = from.left[i] + (to.left[i] - from.left[i]) * ratio;
		result.right[i] = from.right[i]
			+ (to.right[i] - from.right[i]) * ratio;
	}
	result.radius = from.radius + (to.radius - from.radius) * ratio;
	return result;
}

void AddQuad(QPainterPath &path, const Quad &quad, float64 radius) {
	const auto same = [](QPointF a, QPointF b) {
		return (std::abs(a.x() - b.x()) < 0.001)
			&& (std::abs(a.y() - b.y()) < 0.001);
	};
	auto points = Quad();
	auto count = 0;
	for (const auto &point : quad) {
		if (!count || !same(points[count - 1], point)) {
			points[count++] = point;
		}
	}
	if (count > 2 && same(points[0], points[count - 1])) {
		--count;
	}
	auto skeleton = QPainterPath();
	skeleton.moveTo(points[0]);
	for (auto i = 1; i != count; ++i) {
		skeleton.lineTo(points[i]);
	}
	if (count > 2) {
		skeleton.closeSubpath();
	}
	auto stroker = QPainterPathStroker();
	stroker.setWidth(radius * 2.);
	stroker.setJoinStyle(Qt::RoundJoin);
	stroker.setCapStyle(Qt::RoundCap);
	path.addPath(stroker.createStroke(skeleton));
	path.addPath(skeleton);
}

void PaintShape(QPainter &p, const Shape &shape, const QBrush &brush) {
	auto path = QPainterPath();
	AddQuad(path, shape.left, shape.radius);
	AddQuad(path, shape.right, shape.radius);
	path.setFillRule(Qt::WindingFill);
	p.fillPath(path, brush);
}

} // namespace

PlayButtonLayout::PlayButtonLayout(
	const style::MediaPlayerPlayIcon &st,
	Fn<void()> callback)
: _st(st)
, _callback(std::move(callback)) {
}

void PlayButtonLayout::setState(State state) {
	if (_nextState == state) {
		return;
	}
	_nextState = state;
	if (!_transformProgress.animating()) {
		_oldState = _state;
		_state = _nextState;
		_transformBackward = false;
		if (_state != _oldState) {
			startTransform(0., 1.);
			if (_callback) _callback();
		}
	} else if (_oldState == _nextState) {
		std::swap(_oldState, _state);
		startTransform(
			_transformBackward ? 0. : 1.,
			_transformBackward ? 1. : 0.);
		_transformBackward = !_transformBackward;
	}
}

void PlayButtonLayout::finishTransform() {
	_transformProgress.stop();
	_transformBackward = false;
	if (_callback) _callback();
}

void PlayButtonLayout::paint(QPainter &p, const QBrush &brush) {
	const auto shape = [&](State state) {
		switch (state) {
		case State::Play: return PlayShape(_st);
		case State::Pause: return PauseShape(_st);
		case State::Cancel: return CancelShape(_st);
		}
		Unexpected("State in Media::Player::PlayButtonLayout.");
	};
	const auto progress = _transformProgress.value(1.);
	const auto current = !_transformProgress.animating()
		? shape(_state)
		: Interpolate(
			shape(_oldState),
			shape(_state),
			_transformBackward ? (1. - progress) : progress);

	PainterHighQualityEnabler hq(p);
	PaintShape(p, current, brush);
}

void PlayButtonLayout::animationCallback() {
	if (!_transformProgress.animating()) {
		const auto finalState = _nextState;
		_nextState = _state;
		setState(finalState);
	}
	_callback();
}

void PlayButtonLayout::startTransform(float64 from, float64 to) {
	_transformProgress.start(
		[=] { animationCallback(); },
		from,
		to,
		_st.duration);
}

PlayButton::PlayButton(
	QWidget *parent,
	const style::MediaPlayerPlayButton &st)
: RippleButton(parent, st.ripple)
, _st(st)
, _layout(st.icon, [=] { update(); }) {
	resize(st.size);
	setCursor(style::cur_pointer);
}

void PlayButton::setState(State state) {
	_layout.setState(state);
}

void PlayButton::finishTransform() {
	_layout.finishTransform();
}

void PlayButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	paintRipple(p, _st.rippleAreaPosition);
	p.translate(_st.iconPosition);
	_layout.paint(p, _st.color);
}

QPoint PlayButton::prepareRippleStartPosition() const {
	const auto result = mapFromGlobal(QCursor::pos())
		- _st.rippleAreaPosition;
	const auto area = QRect(0, 0, _st.rippleAreaSize, _st.rippleAreaSize);
	return area.contains(result)
		? result
		: DisabledRippleStartPosition();
}

QImage PlayButton::prepareRippleMask() const {
	return Ui::RippleAnimation::EllipseMask(
		QSize(_st.rippleAreaSize, _st.rippleAreaSize));
}

SpeedButtonLayout::SpeedButtonLayout(
	const style::MediaSpeedButton &st,
	Fn<void()> callback,
	float64 speed)
: _st(st)
, _speed(speed)
, _metrics(_st.font->f)
, _text(SpeedText(speed))
, _textWidth(_metrics.horizontalAdvance(_text))
, _callback(std::move(callback)) {
	const auto result = style::FindAdjustResult(_st.font->f);
	_adjustedAscent = result ? result->ascent : _metrics.ascent();
	_adjustedHeight = result ? result->height : _metrics.height();
}

void SpeedButtonLayout::setSpeed(float64 speed) {
	speed = base::SafeRound(speed * 10.) / 10.;
	if (!EqualSpeeds(_speed, speed)) {
		_speed = speed;
		_text = SpeedText(_speed);
		_textWidth = _metrics.horizontalAdvance(_text);
		if (_callback) _callback();
	}
}

void SpeedButtonLayout::paint(QPainter &p, bool over, bool active) {
	const auto &color = active ? _st.activeFg : over ? _st.overFg : _st.fg;
	const auto inner = QRect(QPoint(), _st.size).marginsRemoved(_st.padding);
	_st.icon.paintInCenter(p, inner, color->c);

	p.setPen(color);
	p.setFont(_st.font);

	p.drawText(
		QPointF(inner.topLeft()) + QPointF(
			(inner.width() - _textWidth) / 2.,
			(inner.height() - _adjustedHeight) / 2. + _adjustedAscent),
		_text);
}

SpeedButton::SpeedButton(QWidget *parent, const style::MediaSpeedButton &st)
: RippleButton(parent, st.ripple)
, _st(st)
, _layout(st, [=] { update(); }, 2.)
, _isDefault(true) {
	resize(_st.size);
}

void SpeedButton::setSpeed(float64 speed) {
	_isDefault = EqualSpeeds(speed, 1.);
	_layout.setSpeed(speed);
	update();
}

void SpeedButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	paintRipple(
		p,
		QPoint(_st.padding.left(), _st.padding.top()),
		_isDefault ? nullptr : &_st.rippleActiveColor->c);
	_layout.paint(p, isOver(), !_isDefault);
}

QPoint SpeedButton::prepareRippleStartPosition() const {
	const auto inner = rect().marginsRemoved(_st.padding);
	const auto result = mapFromGlobal(QCursor::pos()) - inner.topLeft();
	return inner.contains(result)
		? result
		: DisabledRippleStartPosition();
}

QImage SpeedButton::prepareRippleMask() const {
	return Ui::RippleAnimation::RoundRectMask(
		rect().marginsRemoved(_st.padding).size(),
		_st.rippleRadius);
}

SettingsButton::SettingsButton(
	QWidget *parent,
	const style::MediaSpeedButton &st)
: RippleButton(parent, st.ripple)
, _st(st)
, _isDefaultSpeed(true) {
	resize(_st.size);
}

void SettingsButton::setSpeed(float64 speed) {
	if (_speed != speed) {
		_speed = speed;
		_isDefaultSpeed = EqualSpeeds(speed, 1.);
		update();
	}
}

void SettingsButton::setQuality(Media::VideoQuality quality) {
	if (_quality != quality) {
		_quality = quality;
		update();
	}
}

void SettingsButton::setActive(bool active) {
	if (_active == active) {
		return;
	}
	_active = active;
	_activeAnimation.start([=] {
		update();
	}, active ? 0. : 1., active ? 1. : 0., st::mediaviewOverDuration);
}

void SettingsButton::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);

	const auto nowOver = isOver();
	const auto wasOver = static_cast<bool>(was & StateFlag::Over);
	if (nowOver != wasOver) {
		_overAnimation.start([=] {
			update();
		}, nowOver ? 0. : 1., nowOver ? 1. : 0., st::mediaviewOverDuration);
	}
}

void SettingsButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	paintRipple(
		p,
		QPoint(_st.padding.left(), _st.padding.top()),
		_isDefaultSpeed ? nullptr : &_st.rippleActiveColor->c);

	prepareFrame();
	p.drawImage(0, 0, _frameCache);
}

void SettingsButton::prepareFrame() {
	const auto ratio = style::DevicePixelRatio();
	if (_frameCache.size() != _st.size * ratio) {
		_frameCache = QImage(
			_st.size * ratio,
			QImage::Format_ARGB32_Premultiplied);
		_frameCache.setDevicePixelRatio(ratio);
	}
	_frameCache.fill(Qt::transparent);
	auto p = QPainter(&_frameCache);

	const auto inner = QRect(
		QPoint(),
		_st.size
	).marginsRemoved(_st.padding);

	auto hq = std::optional<PainterHighQualityEnabler>();
	const auto over = _overAnimation.value(isOver() ? 1. : 0.);
	const auto color = anim::color(_st.fg, _st.overFg, over);
	const auto active = _activeAnimation.value(_active ? 1. : 0.);
	if (active > 0.) {
		const auto shift = QRectF(inner).center();
		p.save();
		p.translate(shift);
		p.rotate(active * 60.);
		p.translate(-shift);
		hq.emplace(p);
	}
	_st.icon.paintInCenter(p, inner, color);
	if (active > 0.) {
		p.restore();
		hq.reset();
	}

	const auto rounded = int(base::SafeRound(_speed * 10));
	if (rounded != 10) {
		const auto text = (rounded % 10)
			? QString::number(rounded / 10.)
			: u"%1X"_q.arg(rounded / 10);
		paintBadge(p, text, RectPart::TopLeft, color);
	}
	const auto height = _quality.height;
	const auto text = !height
		? QString()
		: (height > 2000)
		? u"4K"_q
		: (height > 1000)
		? u"FHD"_q
		: (height > 700)
		? u"HD"_q
		: u"SD"_q;
	if (!text.isEmpty()) {
		paintBadge(p, text, RectPart::BottomRight, color);
	}
}

void SettingsButton::paintBadge(
		QPainter &p,
		const QString &text,
		RectPart origin,
		QColor color) {
	auto hq = PainterHighQualityEnabler(p);
	const auto xpadding = style::ConvertScale(2.);
	const auto ypadding = 0;
	const auto skip = style::ConvertScale(2.);
	const auto width = _st.font->width(text);
	const auto height = _st.font->height;
	const auto radius = height / 3.;
	const auto left = (origin == RectPart::TopLeft)
		|| (origin == RectPart::BottomLeft);
	const auto top = (origin == RectPart::TopLeft)
		|| (origin == RectPart::TopRight);
	const auto x = left ? 0 : (_st.size.width() - width - 2 * xpadding);
	const auto y = top
		? skip
		: (_st.size.height() - height - 2 * ypadding - skip);
	p.setCompositionMode(QPainter::CompositionMode_Source);
	const auto stroke = style::ConvertScaleExact(1.);
	p.setPen(QPen(Qt::transparent, stroke));
	p.setFont(_st.font);
	p.setBrush(color);
	p.drawRoundedRect(
		QRectF(
			x - stroke / 2.,
			y - stroke / 2.,
			width + 2 * xpadding + stroke,
			height + 2 * ypadding + stroke),
		radius,
		radius);
	p.setPen(Qt::transparent);
	p.drawText(x + xpadding, y + ypadding + _st.font->ascent, text);
}

QPoint SettingsButton::prepareRippleStartPosition() const {
	const auto inner = rect().marginsRemoved(_st.padding);
	const auto result = mapFromGlobal(QCursor::pos()) - inner.topLeft();
	return inner.contains(result)
		? result
		: DisabledRippleStartPosition();
}

QImage SettingsButton::prepareRippleMask() const {
	return Ui::RippleAnimation::RoundRectMask(
		rect().marginsRemoved(_st.padding).size(),
		_st.rippleRadius);
}

} // namespace Media::Player
