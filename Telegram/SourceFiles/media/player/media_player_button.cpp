/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "media/player/media_player_button.h"

#include "styles/style_media_player.h"
#include "media/media_audio.h"
#include "media/player/media_player_instance.h"
#include "shortcuts.h"

namespace Media {
namespace Player {

TitleButton::TitleButton(QWidget *parent) : Button(parent) {
	setAttribute(Qt::WA_OpaquePaintEvent);
	resize(st::mediaPlayerTitleButtonSize);

	setClickedCallback([this]() {
		if (exists()) {
			if (_showPause) {
				instance()->pause();
			} else {
				instance()->play();
			}
		}
	});

	if (exists()) {
		subscribe(instance()->updatedNotifier(), [this](const UpdatedEvent &e) {
			updatePauseState();
		});
		updatePauseState();
		finishIconTransform();
	}
}

void TitleButton::updatePauseState() {
	AudioMsgId playing;
	auto playbackState = audioPlayer()->currentState(&playing, AudioMsgId::Type::Song);
	auto stopped = ((playbackState.state & AudioPlayerStoppedMask) || playbackState.state == AudioPlayerFinishing);
	auto showPause = !stopped && (playbackState.state == AudioPlayerPlaying || playbackState.state == AudioPlayerResuming || playbackState.state == AudioPlayerStarting);
	if (exists() && instance()->isSeeking()) {
		showPause = true;
	}
	setShowPause(showPause);
}

void TitleButton::setShowPause(bool showPause) {
	if (_showPause != showPause) {
		_showPause = showPause;
		_iconTransformToPause.start([this] { update(); }, _showPause ? 0. : 1., _showPause ? 1. : 0., st::mediaPlayerTitleButtonTransformDuration);
		update();
	}
}

void TitleButton::finishIconTransform() {
	if (_iconTransformToPause.animating(getms())) {
		_iconTransformToPause.finish();
		update();
	}
}

void TitleButton::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(rect(), st::titleBg);

	p.setBrush(st::mediaPlayerTitleButtonInnerBg);
	p.setPen(Qt::NoPen);

	p.setRenderHint(QPainter::HighQualityAntialiasing, true);
	p.drawEllipse((width() - st::mediaPlayerTitleButtonInner.width()) / 2, (height() - st::mediaPlayerTitleButtonInner.height()) / 2, st::mediaPlayerTitleButtonInner.width(), st::mediaPlayerTitleButtonInner.height());
	p.setRenderHint(QPainter::HighQualityAntialiasing, false);

	paintIcon(p);
}

void TitleButton::onStateChanged(int oldState, ButtonStateChangeSource source) {
	if ((oldState & StateOver) != (_state & StateOver)) {
		auto over = (_state & StateOver);
		_iconFg.start([this] { update(); }, over ? st::titleButtonFg->c : st::titleButtonActiveFg->c, over ? st::titleButtonActiveFg->c : st::titleButtonFg->c, st::titleButtonDuration);
	}
}

namespace {

template <int N>
QPainterPath interpolatePaths(QPointF (&from)[N], QPointF (&to)[N], float64 k) {
	static_assert(N > 1, "Wrong points count in path!");

	auto from_coef = k, to_coef = 1. - k;
	QPainterPath result;
	auto x = from[0].x() * from_coef + to[0].x() * to_coef;
	auto y = from[0].y() * from_coef + to[0].y() * to_coef;
	result.moveTo(x, y);
	for (int i = 1; i != N; ++i) {
		result.lineTo(from[i].x() * from_coef + to[i].x() * to_coef, from[i].y() * from_coef + to[i].y() * to_coef);
	}
	result.lineTo(x, y);
	return result;
}

} // namespace

void TitleButton::paintIcon(Painter &p) {
	auto over = (_state & StateOver);
	auto icon = _iconFg.current(getms(), over ? st::titleButtonActiveFg->c : st::titleButtonFg->c);
	auto showPause = _iconTransformToPause.current(getms(), _showPause ? 1. : 0.);
	auto pauseWidth = st::mediaPlayerTitleButtonInner.width() - 2 * st::mediaPlayerTitleButtonPauseLeft;
	auto playWidth = pauseWidth;
	auto pauseHeight = st::mediaPlayerTitleButtonInner.height() - 2 * st::mediaPlayerTitleButtonPauseTop;
	auto playHeight = st::mediaPlayerTitleButtonInner.height() - 2 * st::mediaPlayerTitleButtonPlayTop;
	auto pauseStroke = st::mediaPlayerTitleButtonPauseStroke;

	qreal left = (width() - st::mediaPlayerTitleButtonInner.width()) / 2;
	qreal top = (height() - st::mediaPlayerTitleButtonInner.height()) / 2;

	auto pauseLeft = left + st::mediaPlayerTitleButtonPauseLeft;
	auto playLeft = left + st::mediaPlayerTitleButtonPlayLeft;
	auto pauseTop = top + st::mediaPlayerTitleButtonPauseTop;
	auto playTop = top + st::mediaPlayerTitleButtonPlayTop;

	p.setRenderHint(QPainter::HighQualityAntialiasing, true);
	p.setPen(Qt::NoPen);

	if (showPause == 0.) {
		QPainterPath pathPlay;
		pathPlay.moveTo(playLeft, playTop);
		pathPlay.lineTo(playLeft + playWidth, playTop + (playHeight / 2.));
		pathPlay.lineTo(playLeft, playTop + playHeight);
		pathPlay.lineTo(playLeft, playTop);
		p.fillPath(pathPlay, icon);
	} else {
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
		p.fillPath(interpolatePaths(pathLeftPause, pathLeftPlay, showPause), icon);

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
		p.fillPath(interpolatePaths(pathRightPause, pathRightPlay, showPause), icon);
	}
	p.setRenderHint(QPainter::HighQualityAntialiasing, false);
}

} // namespace Player
} // namespace Media
