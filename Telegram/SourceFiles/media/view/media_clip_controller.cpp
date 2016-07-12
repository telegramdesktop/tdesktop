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
	connect(_playback, SIGNAL(seekProgress(int64)), this, SLOT(onSeekProgress(int64)));
	connect(_playback, SIGNAL(seekFinished(int64)), this, SLOT(onSeekFinished(int64)));
	connect(_volumeController, SIGNAL(volumeChanged(float64)), this, SIGNAL(volumeChanged(float64)));
}

void Controller::onSeekProgress(int64 position) {
	_seekPosition = position;
	emit seekProgress(position);
}

void Controller::onSeekFinished(int64 position) {
	_seekPosition = -1;
	emit seekFinished(position);
}

void Controller::showAnimated() {
	startFading([this]() {
		_fadeAnimation->fadeIn(st::mvShowDuration);
	});
}

void Controller::hideAnimated() {
	startFading([this]() {
		_fadeAnimation->fadeOut(st::mvShowDuration);
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

void Controller::updatePlayback(const AudioPlaybackState &playbackState) {
	updatePlayPauseResumeState(playbackState);
	_playback->updateState(playbackState);
	updateTimeTexts(playbackState);
}

void Controller::updatePlayPauseResumeState(const AudioPlaybackState &playbackState) {
	bool showPause = (playbackState.state == AudioPlayerPlaying || playbackState.state == AudioPlayerResuming);
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

	auto timeAlready = formatDurationText(playAlready);
	auto minus = QChar(8722);
	auto timeLeft = minus + formatDurationText(playLeft);

	auto alreadyChanged = false, leftChanged = false;
	_playedAlready->setText(timeAlready, &alreadyChanged);
	_toPlayLeft->setText(timeLeft, &leftChanged);
	if (alreadyChanged || leftChanged) {
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
	_playPauseResume->moveToLeft(playTop, playTop);

	int fullScreenTop = (height() - _fullScreenToggle->height()) / 2;
	_fullScreenToggle->moveToRight(fullScreenTop, fullScreenTop);

	_volumeController->moveToRight(fullScreenTop + _fullScreenToggle->width() + fullScreenTop, (height() - _volumeController->height()) / 2);
	_playback->resize(width() - playTop - _playPauseResume->width() - playTop - fullScreenTop - _volumeController->width() - fullScreenTop - _fullScreenToggle->width() - fullScreenTop, _volumeController->height());
	_playback->moveToLeft(playTop + _playPauseResume->width() + playTop, st::mediaviewPlaybackTop);

	_playedAlready->moveToLeft(playTop + _playPauseResume->width() + playTop, st::mediaviewPlayProgressTop);
	_toPlayLeft->moveToRight(width() - (playTop + _playPauseResume->width() + playTop) - _playback->width(), st::mediaviewPlayProgressTop);
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

} // namespace Clip
} // namespace Media
