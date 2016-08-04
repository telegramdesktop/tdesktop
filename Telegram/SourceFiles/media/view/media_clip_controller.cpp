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
#include "media/view/media_clip_controller.h"

#include "media/view/media_clip_playback.h"
#include "media/view/media_clip_volume_controller.h"
#include "styles/style_mediaview.h"
#include "ui/widgets/label_simple.h"
#include "ui/effects/fade_animation.h"
#include "ui/buttons/icon_button.h"
#include "media/media_audio.h"

namespace Media {
namespace Clip {

Controller::Controller(QWidget *parent) : TWidget(parent)
, _playPauseResume(this, st::mediaviewPlayButton)
, _playback(this)
, _volumeController(this)
, _fullScreenToggle(this, st::mediaviewFullScreenButton)
, _playedAlready(this, st::mediaviewPlayProgressLabel)
, _toPlayLeft(this, st::mediaviewPlayProgressLabel)
, _fadeAnimation(std_::make_unique<Ui::FadeAnimation>(this)) {
	_fadeAnimation->show();
	_fadeAnimation->setFinishedCallback(func(this, &Controller::fadeFinished));
	_fadeAnimation->setUpdatedCallback(func(this, &Controller::fadeUpdated));

	_volumeController->setVolume(Global::VideoVolume());

	connect(_playPauseResume, SIGNAL(clicked()), this, SIGNAL(playPressed()));
	connect(_fullScreenToggle, SIGNAL(clicked()), this, SIGNAL(toFullScreenPressed()));
	connect(_playback, SIGNAL(seekProgress(float64)), this, SLOT(onSeekProgress(float64)));
	connect(_playback, SIGNAL(seekFinished(float64)), this, SLOT(onSeekFinished(float64)));
	connect(_volumeController, SIGNAL(volumeChanged(float64)), this, SIGNAL(volumeChanged(float64)));
}

void Controller::onSeekProgress(float64 progress) {
	if (!_lastDurationMs) return;

	auto positionMs = snap(static_cast<int64>(progress * _lastDurationMs), 0LL, _lastDurationMs);
	if (_seekPositionMs != positionMs) {
		_seekPositionMs = positionMs;
		refreshTimeTexts();
		emit seekProgress(positionMs); // This may destroy Controller.
	}
}

void Controller::onSeekFinished(float64 progress) {
	if (!_lastDurationMs) return;

	auto positionMs = snap(static_cast<int64>(progress * _lastDurationMs), 0LL, _lastDurationMs);
	_seekPositionMs = -1;
	emit seekFinished(positionMs);
	refreshTimeTexts();
}

void Controller::showAnimated() {
	startFading([this]() {
		_fadeAnimation->fadeIn(st::mvShowDuration);
	});
}

void Controller::hideAnimated() {
	startFading([this]() {
		_fadeAnimation->fadeOut(st::mvHideDuration);
	});
}

template <typename Callback>
void Controller::startFading(Callback start) {
	start();
	_playback->show();
}

void Controller::fadeFinished() {
	fadeUpdated(1.);
}

void Controller::fadeUpdated(float64 opacity) {
	_playback->setFadeOpacity(opacity);
}

void Controller::updatePlayback(const AudioPlaybackState &playbackState, bool reset) {
	updatePlayPauseResumeState(playbackState);
	_playback->updateState(playbackState, reset);
	updateTimeTexts(playbackState);
}

void Controller::updatePlayPauseResumeState(const AudioPlaybackState &playbackState) {
	bool showPause = (playbackState.state == AudioPlayerPlaying || playbackState.state == AudioPlayerResuming || _seekPositionMs >= 0);
	if (showPause != _showPause) {
		disconnect(_playPauseResume, SIGNAL(clicked()), this, _showPause ? SIGNAL(pausePressed()) : SIGNAL(playPressed()));
		_showPause = showPause;
		connect(_playPauseResume, SIGNAL(clicked()), this, _showPause ? SIGNAL(pausePressed()) : SIGNAL(playPressed()));

		_playPauseResume->setIcon(_showPause ? &st::mediaviewPauseIcon : nullptr);
	}
}

void Controller::updateTimeTexts(const AudioPlaybackState &playbackState) {
	qint64 position = 0, duration = playbackState.duration;

	if (!(playbackState.state & AudioPlayerStoppedMask) && playbackState.state != AudioPlayerFinishing) {
		position = playbackState.position;
	} else if (playbackState.state == AudioPlayerStoppedAtEnd) {
		position = playbackState.duration;
	} else {
		position = 0;
	}
	auto playFrequency = (playbackState.frequency ? playbackState.frequency : AudioVoiceMsgFrequency);
	auto playAlready = position / playFrequency;
	auto playLeft = (playbackState.duration / playFrequency) - playAlready;

	_lastDurationMs = (playbackState.duration * 1000LL) / playFrequency;

	_timeAlready = formatDurationText(playAlready);
	auto minus = QChar(8722);
	_timeLeft = minus + formatDurationText(playLeft);

	if (_seekPositionMs < 0) {
		refreshTimeTexts();
	}
}

void Controller::refreshTimeTexts() {
	auto alreadyChanged = false, leftChanged = false;
	auto timeAlready = _timeAlready;
	auto timeLeft = _timeLeft;
	if (_seekPositionMs >= 0) {
		auto playAlready = _seekPositionMs / 1000LL;
		auto playLeft = (_lastDurationMs / 1000LL) - playAlready;

		timeAlready = formatDurationText(playAlready);
		auto minus = QChar(8722);
		timeLeft = minus + formatDurationText(playLeft);
	}

	_playedAlready->setText(timeAlready, &alreadyChanged);
	_toPlayLeft->setText(timeLeft, &leftChanged);
	if (alreadyChanged || leftChanged) {
		resizeEvent(nullptr);
		_fadeAnimation->refreshCache();
	}
}

void Controller::setInFullScreen(bool inFullScreen) {
	_fullScreenToggle->setIcon(inFullScreen ? &st::mediaviewFullScreenOutIcon : nullptr);
	disconnect(_fullScreenToggle, SIGNAL(clicked()), this, SIGNAL(toFullScreenPressed()));
	disconnect(_fullScreenToggle, SIGNAL(clicked()), this, SIGNAL(fromFullScreenPressed()));

	auto handler = inFullScreen ? SIGNAL(fromFullScreenPressed()) : SIGNAL(toFullScreenPressed());
	connect(_fullScreenToggle, SIGNAL(clicked()), this, handler);
}

void Controller::grabStart() {
	showChildren();
	_playback->hide();
}

void Controller::grabFinish() {
	hideChildren();
	_playback->show();
}

void Controller::resizeEvent(QResizeEvent *e) {
	int playTop = (height() - _playPauseResume->height()) / 2;
	_playPauseResume->moveToLeft(st::mediaviewPlayPauseLeft, playTop);

	int fullScreenTop = (height() - _fullScreenToggle->height()) / 2;
	_fullScreenToggle->moveToRight(st::mediaviewFullScreenLeft, fullScreenTop);

	_volumeController->moveToRight(st::mediaviewFullScreenLeft + _fullScreenToggle->width() + st::mediaviewVolumeLeft, (height() - _volumeController->height()) / 2);
	_playback->resize(width() - st::mediaviewPlayPauseLeft - _playPauseResume->width() - playTop - fullScreenTop - _volumeController->width() - st::mediaviewVolumeLeft - _fullScreenToggle->width() - st::mediaviewFullScreenLeft, st::mediaviewSeekSize.height());
	_playback->moveToLeft(st::mediaviewPlayPauseLeft + _playPauseResume->width() + playTop, st::mediaviewPlaybackTop);

	_playedAlready->moveToLeft(st::mediaviewPlayPauseLeft + _playPauseResume->width() + playTop, st::mediaviewPlayProgressTop);
	_toPlayLeft->moveToRight(width() - (st::mediaviewPlayPauseLeft + _playPauseResume->width() + playTop) - _playback->width(), st::mediaviewPlayProgressTop);
}

void Controller::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (_fadeAnimation->paint(p)) {
		return;
	}

	App::roundRect(p, rect(), st::medviewSaveMsg, MediaviewSaveCorners);
}

void Controller::mousePressEvent(QMouseEvent *e) {
	e->accept(); // Don't pass event to the MediaView.
}

Controller::~Controller() {
}

} // namespace Clip
} // namespace Media
