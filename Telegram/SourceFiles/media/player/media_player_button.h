/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/abstract_button.h"
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
	PlayButtonLayout(const style::MediaPlayerButton &st, base::lambda<void()> callback);

	void setState(State state);
	void finishTransform();
	void paint(Painter &p, const QBrush &brush);

private:
	void animationCallback();
	void startTransform(float64 from, float64 to);

	void paintPlay(Painter &p, const QBrush &brush);
	void paintPlayToPause(Painter &p, const QBrush &brush, float64 progress);
	void paintPlayToCancel(Painter &p, const QBrush &brush, float64 progress);
	void paintPauseToCancel(Painter &p, const QBrush &brush, float64 progress);

	const style::MediaPlayerButton &_st;

	State _state = State::Play;
	State _oldState = State::Play;
	State _nextState = State::Play;
	Animation _transformProgress;
	bool _transformBackward = false;

	base::lambda<void()> _callback;

};

} // namespace Clip
} // namespace Media
