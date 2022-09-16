/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/abstract_button.h"
#include "ui/effects/animations.h"
#include "styles/style_media_player.h"

namespace Media {
namespace Player {

class PlayButtonLayout {
public:
	enum class State {
		Play,
		Pause,
		Cancel,
	};
	PlayButtonLayout(const style::MediaPlayerButton &st, Fn<void()> callback);

	void setState(State state);
	void finishTransform();
	void paint(QPainter &p, const QBrush &brush);

private:
	void animationCallback();
	void startTransform(float64 from, float64 to);

	void paintPlay(QPainter &p, const QBrush &brush);
	void paintPlayToPause(QPainter &p, const QBrush &brush, float64 progress);
	void paintPlayToCancel(QPainter &p, const QBrush &brush, float64 progress);
	void paintPauseToCancel(QPainter &p, const QBrush &brush, float64 progress);

	const style::MediaPlayerButton &_st;

	State _state = State::Play;
	State _oldState = State::Play;
	State _nextState = State::Play;
	Ui::Animations::Simple _transformProgress;
	bool _transformBackward = false;

	Fn<void()> _callback;

};

} // namespace Player
} // namespace Media
