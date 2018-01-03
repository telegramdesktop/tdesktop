/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/player/media_player_button.h"

#include "styles/style_widgets.h"

namespace Media {
namespace Player {

PlayButtonLayout::PlayButtonLayout(const style::MediaPlayerButton &st, base::lambda<void()> callback)
: _st(st)
, _callback(std::move(callback)) {
}

void PlayButtonLayout::setState(State state) {
	if (_nextState == state) return;

	_nextState = state;
	if (!_transformProgress.animating(getms())) {
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
	_transformProgress.finish();
	_transformBackward = false;
	if (_callback) _callback();
}

void PlayButtonLayout::paint(Painter &p, const QBrush &brush) {
	if (_transformProgress.animating(getms())) {
		auto from = _oldState, to = _state;
		auto backward = _transformBackward;
		auto progress = _transformProgress.current(1.);
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

void PlayButtonLayout::paintPlay(Painter &p, const QBrush &brush) {
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

void PlayButtonLayout::paintPlayToPause(Painter &p, const QBrush &brush, float64 progress) {
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

void PlayButtonLayout::paintPlayToCancel(Painter &p, const QBrush &brush, float64 progress) {
	static const auto sqrt2 = sqrt(2.);

	auto playLeft = 0. + _st.playPosition.x();
	auto playTop = 0. + _st.playPosition.y();
	auto playWidth = _st.playOuter.width() - 2 * playLeft;
	auto playHeight = _st.playOuter.height() - 2 * playTop;

	auto cancelLeft = 0. + _st.cancelPosition.x();
	auto cancelTop = 0. + _st.cancelPosition.y();
	auto cancelWidth = _st.cancelOuter.width() - 2 * cancelLeft;
	auto cancelHeight = _st.cancelOuter.height() - 2 * cancelTop;
	auto cancelStroke = (0. + _st.cancelStroke) / sqrt2;

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

void PlayButtonLayout::paintPauseToCancel(Painter &p, const QBrush &brush, float64 progress) {
	static const auto sqrt2 = sqrt(2.);

	auto pauseLeft = 0. + _st.pausePosition.x();
	auto pauseTop = 0. + _st.pausePosition.y();
	auto pauseWidth = _st.pauseOuter.width() - 2 * pauseLeft;
	auto pauseHeight = _st.pauseOuter.height() - 2 * pauseTop;
	auto pauseStroke = 0. + _st.pauseStroke;

	auto cancelLeft = 0. + _st.cancelPosition.x();
	auto cancelTop = 0. + _st.cancelPosition.y();
	auto cancelWidth = _st.cancelOuter.width() - 2 * cancelLeft;
	auto cancelHeight = _st.cancelOuter.height() - 2 * cancelTop;
	auto cancelStroke = (0. + _st.cancelStroke) / sqrt2;

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
	_transformProgress.start([this] { animationCallback(); }, from, to, st::mediaPlayerButtonTransformDuration);
}

} // namespace Player
} // namespace Media
