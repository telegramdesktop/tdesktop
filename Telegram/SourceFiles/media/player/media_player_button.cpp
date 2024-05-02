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

#include <QtCore/QtMath>

namespace Media::Player {
namespace {

[[nodiscard]] QString SpeedText(float64 speed) {
	return QString::number(base::SafeRound(speed * 10) / 10.) + 'X';
}

} // namespace

PlayButtonLayout::PlayButtonLayout(
	const style::MediaPlayerButton &st,
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
		qSwap(_oldState, _state);
		startTransform(_transformBackward ? 0. : 1., _transformBackward ? 1. : 0.);
		_transformBackward = !_transformBackward;
	}
}

void PlayButtonLayout::finishTransform() {
	_transformProgress.stop();
	_transformBackward = false;
	if (_callback) _callback();
}

void PlayButtonLayout::paint(QPainter &p, const QBrush &brush) {
	if (_transformProgress.animating()) {
		auto from = _oldState, to = _state;
		auto backward = _transformBackward;
		auto progress = _transformProgress.value(1.);
		if (from == State::Cancel || (from == State::Pause && to == State::Play)) {
			qSwap(from, to);
			backward = !backward;
		}
		if (backward) progress = 1. - progress;

		Assert(from != to);
		if (from == State::Play) {
			if (to == State::Pause) {
				paintPlayToPause(p, brush, progress);
			} else {
				Assert(to == State::Cancel);
				paintPlayToCancel(p, brush, progress);
			}
		} else {
			Assert(from == State::Pause && to == State::Cancel);
			paintPauseToCancel(p, brush, progress);
		}
	} else {
		switch (_state) {
		case State::Play: paintPlay(p, brush); break;
		case State::Pause: paintPlayToPause(p, brush, 1.); break;
		case State::Cancel: paintPlayToCancel(p, brush, 1.); break;
		}
	}
}

void PlayButtonLayout::paintPlay(QPainter &p, const QBrush &brush) {
	auto playLeft = 0. + _st.playPosition.x();
	auto playTop = 0. + _st.playPosition.y();
	auto playWidth = _st.playOuter.width() - 2 * playLeft;
	auto playHeight = _st.playOuter.height() - 2 * playTop;

	PainterHighQualityEnabler hq(p);

	p.setPen(Qt::NoPen);

	QPainterPath pathPlay;
	pathPlay.moveTo(playLeft, playTop);
	pathPlay.lineTo(playLeft + playWidth, playTop + (playHeight / 2.));
	pathPlay.lineTo(playLeft, playTop + playHeight);
	pathPlay.lineTo(playLeft, playTop);
	p.fillPath(pathPlay, brush);
}

void PlayButtonLayout::paintPlayToPause(QPainter &p, const QBrush &brush, float64 progress) {
	auto playLeft = 0. + _st.playPosition.x();
	auto playTop = 0. + _st.playPosition.y();
	auto playWidth = _st.playOuter.width() - 2 * playLeft;
	auto playHeight = _st.playOuter.height() - 2 * playTop;

	auto pauseLeft = 0. + _st.pausePosition.x();
	auto pauseTop = 0. + _st.pausePosition.y();
	auto pauseWidth = _st.pauseOuter.width() - 2 * pauseLeft;
	auto pauseHeight = _st.pauseOuter.height() - 2 * pauseTop;
	auto pauseStroke = 0. + _st.pauseStroke;

	p.setPen(Qt::NoPen);
	PainterHighQualityEnabler hq(p);

	QPointF pathLeftPause[] = {
		{ pauseLeft, pauseTop },
		{ pauseLeft + pauseStroke, pauseTop },
		{ pauseLeft + pauseStroke, pauseTop + pauseHeight },
		{ pauseLeft, pauseTop + pauseHeight },
	};
	QPointF pathLeftPlay[] = {
		{ playLeft, playTop },
		{ playLeft + (playWidth / 2.), playTop + (playHeight / 4.) },
		{ playLeft + (playWidth / 2.), playTop + (3 * playHeight / 4.) },
		{ playLeft, playTop + playHeight },
	};
	p.fillPath(anim::interpolate(pathLeftPlay, pathLeftPause, progress), brush);

	QPointF pathRightPause[] = {
		{ pauseLeft + pauseWidth - pauseStroke, pauseTop },
		{ pauseLeft + pauseWidth, pauseTop },
		{ pauseLeft + pauseWidth, pauseTop + pauseHeight },
		{ pauseLeft + pauseWidth - pauseStroke, pauseTop + pauseHeight },
	};
	QPointF pathRightPlay[] = {
		{ playLeft + (playWidth / 2.), playTop + (playHeight / 4.) },
		{ playLeft + playWidth, playTop + (playHeight / 2.) },
		{ playLeft + playWidth, playTop + (playHeight / 2.) },
		{ playLeft + (playWidth / 2.), playTop + (3 * playHeight / 4.) },
	};
	p.fillPath(anim::interpolate(pathRightPlay, pathRightPause, progress), brush);
}

void PlayButtonLayout::paintPlayToCancel(QPainter &p, const QBrush &brush, float64 progress) {
	auto playLeft = 0. + _st.playPosition.x();
	auto playTop = 0. + _st.playPosition.y();
	auto playWidth = _st.playOuter.width() - 2 * playLeft;
	auto playHeight = _st.playOuter.height() - 2 * playTop;

	auto cancelLeft = 0. + _st.cancelPosition.x();
	auto cancelTop = 0. + _st.cancelPosition.y();
	auto cancelWidth = _st.cancelOuter.width() - 2 * cancelLeft;
	auto cancelHeight = _st.cancelOuter.height() - 2 * cancelTop;
	auto cancelStroke = (0. + _st.cancelStroke) / M_SQRT2;

	p.setPen(Qt::NoPen);
	PainterHighQualityEnabler hq(p);

	QPointF pathPlay[] = {
		{ playLeft, playTop },
		{ playLeft, playTop },
		{ playLeft + (playWidth / 2.), playTop + (playHeight / 4.) },
		{ playLeft + playWidth, playTop + (playHeight / 2.) },
		{ playLeft + playWidth, playTop + (playHeight / 2.) },
		{ playLeft + playWidth, playTop + (playHeight / 2.) },
		{ playLeft + playWidth, playTop + (playHeight / 2.) },
		{ playLeft + playWidth, playTop + (playHeight / 2.) },
		{ playLeft + (playWidth / 2.), playTop + (3 * playHeight / 4.) },
		{ playLeft, playTop + playHeight },
		{ playLeft, playTop + playHeight },
		{ playLeft, playTop + (playHeight / 2.) },
	};
	QPointF pathCancel[] = {
		{ cancelLeft, cancelTop + cancelStroke },
		{ cancelLeft + cancelStroke, cancelTop },
		{ cancelLeft + (cancelWidth / 2.), cancelTop + (cancelHeight / 2.) - cancelStroke },
		{ cancelLeft + cancelWidth - cancelStroke, cancelTop },
		{ cancelLeft + cancelWidth, cancelTop + cancelStroke },
		{ cancelLeft + (cancelWidth / 2.) + cancelStroke, cancelTop + (cancelHeight / 2.) },
		{ cancelLeft + cancelWidth, cancelTop + cancelHeight - cancelStroke },
		{ cancelLeft + cancelWidth - cancelStroke, cancelTop + cancelHeight },
		{ cancelLeft + (cancelWidth / 2.), cancelTop + (cancelHeight / 2.) + cancelStroke },
		{ cancelLeft + cancelStroke, cancelTop + cancelHeight },
		{ cancelLeft, cancelTop + cancelHeight - cancelStroke },
		{ cancelLeft + (cancelWidth / 2.) - cancelStroke, cancelTop + (cancelHeight / 2.) },
	};
	p.fillPath(anim::interpolate(pathPlay, pathCancel, progress), brush);
}

void PlayButtonLayout::paintPauseToCancel(QPainter &p, const QBrush &brush, float64 progress) {
	auto pauseLeft = 0. + _st.pausePosition.x();
	auto pauseTop = 0. + _st.pausePosition.y();
	auto pauseWidth = _st.pauseOuter.width() - 2 * pauseLeft;
	auto pauseHeight = _st.pauseOuter.height() - 2 * pauseTop;
	auto pauseStroke = 0. + _st.pauseStroke;

	auto cancelLeft = 0. + _st.cancelPosition.x();
	auto cancelTop = 0. + _st.cancelPosition.y();
	auto cancelWidth = _st.cancelOuter.width() - 2 * cancelLeft;
	auto cancelHeight = _st.cancelOuter.height() - 2 * cancelTop;
	auto cancelStroke = (0. + _st.cancelStroke) / M_SQRT2;

	p.setPen(Qt::NoPen);
	PainterHighQualityEnabler hq(p);

	QPointF pathLeftPause[] = {
		{ pauseLeft, pauseTop },
		{ pauseLeft + pauseStroke, pauseTop },
		{ pauseLeft + pauseStroke, pauseTop + pauseHeight },
		{ pauseLeft, pauseTop + pauseHeight },
	};
	QPointF pathLeftCancel[] = {
		{ cancelLeft, cancelTop + cancelStroke },
		{ cancelLeft + cancelStroke, cancelTop },
		{ cancelLeft + cancelWidth, cancelTop + cancelHeight - cancelStroke },
		{ cancelLeft + cancelWidth - cancelStroke, cancelTop + cancelHeight },
	};
	p.fillPath(anim::interpolate(pathLeftPause, pathLeftCancel, progress), brush);

	QPointF pathRightPause[] = {
		{ pauseLeft + pauseWidth - pauseStroke, pauseTop },
		{ pauseLeft + pauseWidth, pauseTop },
		{ pauseLeft + pauseWidth, pauseTop + pauseHeight },
		{ pauseLeft + pauseWidth - pauseStroke, pauseTop + pauseHeight },
	};
	QPointF pathRightCancel[] = {
		{ cancelLeft + cancelWidth - cancelStroke, cancelTop },
		{ cancelLeft + cancelWidth, cancelTop + cancelStroke },
		{ cancelLeft + cancelStroke, cancelTop + cancelHeight },
		{ cancelLeft, cancelTop + cancelHeight - cancelStroke },
	};
	p.fillPath(anim::interpolate(pathRightPause, pathRightCancel, progress), brush);
}

void PlayButtonLayout::animationCallback() {
	if (!_transformProgress.animating()) {
		auto finalState = _nextState;
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

void SpeedButton::setSpeed(float64 speed, anim::type animated) {
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

} // namespace Media::Player
