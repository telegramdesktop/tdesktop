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
#include "media/player/media_player_title_button.h"

#include "media/player/media_player_button.h"
#include "media/media_audio.h"
#include "media/player/media_player_instance.h"
#include "shortcuts.h"

namespace Media {
namespace Player {

using State = PlayButtonLayout::State;

TitleButton::TitleButton(QWidget *parent) : Button(parent)
, _layout(std_::make_unique<PlayButtonLayout>(st::mediaPlayerTitleButton, [this] { update(); })) {
	setAttribute(Qt::WA_OpaquePaintEvent);
	resize(st::mediaPlayerTitleButtonSize);

	setClickedCallback([this]() {
		if (exists()) {
			instance()->playPauseCancelClicked();
		}
	});

	if (exists()) {
		subscribe(instance()->updatedNotifier(), [this](const UpdatedEvent &e) {
			updatePauseState();
		});
		updatePauseState();
		_layout->finishTransform();
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
	auto state = [audio = playing.audio(), showPause] {
		if (audio && audio->loading()) {
			return State::Cancel;
		} else if (showPause) {
			return State::Pause;
		}
		return State::Play;
	};
	_layout->setState(state());
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

void TitleButton::paintIcon(Painter &p) {
	auto over = (_state & StateOver);
	auto icon = _iconFg.current(getms(), over ? st::titleButtonActiveFg->c : st::titleButtonFg->c);

	auto left = (width() - st::mediaPlayerTitleButtonInner.width()) / 2;
	auto top = (height() - st::mediaPlayerTitleButtonInner.height()) / 2;
	p.translate(left, top);

	_layout->paint(p, icon);
}

void TitleButton::enterEvent(QEvent *e) {
	if (exists()) {
		instance()->titleButtonOver().notify(true, true);
	}
	return Button::enterEvent(e);
}

void TitleButton::leaveEvent(QEvent *e) {
	if (exists()) {
		instance()->titleButtonOver().notify(false, true);
	}
	return Button::leaveEvent(e);
}

TitleButton::~TitleButton() = default;

} // namespace Player
} // namespace Media
