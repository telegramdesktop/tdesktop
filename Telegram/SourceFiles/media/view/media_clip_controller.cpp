/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_clip_controller.h"

#include "media/view/media_clip_playback.h"
#include "styles/style_mediaview.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/effects/fade_animation.h"
#include "ui/widgets/buttons.h"
#include "media/media_audio.h"
#include "layout.h"

namespace Media {
namespace Clip {

Controller::Controller(QWidget *parent) : TWidget(parent)
, _playPauseResume(this, st::mediaviewPlayButton)
, _playbackSlider(this, st::mediaviewPlayback)
, _playback(std::make_unique<Playback>())
, _volumeController(this, st::mediaviewPlayback)
, _fullScreenToggle(this, st::mediaviewFullScreenButton)
, _playedAlready(this, st::mediaviewPlayProgressLabel)
, _toPlayLeft(this, st::mediaviewPlayProgressLabel)
, _fadeAnimation(std::make_unique<Ui::FadeAnimation>(this)) {
	_fadeAnimation->show();
	_fadeAnimation->setFinishedCallback([this] { fadeFinished(); });
	_fadeAnimation->setUpdatedCallback([this](float64 opacity) { fadeUpdated(opacity); });

	_volumeController->setValue(Global::VideoVolume());
	_volumeController->setChangeProgressCallback([=](float64 value) {
		volumeChanged(value);
	});
	//_volumeController->setChangeFinishedCallback();

	connect(_playPauseResume, SIGNAL(clicked()), this, SIGNAL(playPressed()));
	connect(_fullScreenToggle, SIGNAL(clicked()), this, SIGNAL(toFullScreenPressed()));
	//connect(_volumeController, SIGNAL(volumeChanged(float64)), this, SIGNAL(volumeChanged(float64)));

	_playback->setInLoadingStateChangedCallback([this](bool loading) {
		_playbackSlider->setDisabled(loading);
	});
	_playback->setValueChangedCallback([this](float64 value) {
		_playbackSlider->setValue(value);
	});
	_playbackSlider->setChangeProgressCallback([this](float64 value) {
		_playback->setValue(value, false);
		handleSeekProgress(value); // This may destroy Controller.
	});
	_playbackSlider->setChangeFinishedCallback([this](float64 value) {
		_playback->setValue(value, false);
		handleSeekFinished(value);
	});
}

void Controller::handleSeekProgress(float64 progress) {
	if (!_lastDurationMs) return;

	auto positionMs = snap(static_cast<TimeMs>(progress * _lastDurationMs), 0LL, _lastDurationMs);
	if (_seekPositionMs != positionMs) {
		_seekPositionMs = positionMs;
		refreshTimeTexts();
		emit seekProgress(positionMs); // This may destroy Controller.
	}
}

void Controller::handleSeekFinished(float64 progress) {
	if (!_lastDurationMs) return;

	auto positionMs = snap(static_cast<TimeMs>(progress * _lastDurationMs), 0LL, _lastDurationMs);
	_seekPositionMs = -1;
	emit seekFinished(positionMs);
	refreshTimeTexts();
}

template <typename Callback>
void Controller::startFading(Callback start) {
	if (!_fadeAnimation->animating()) {
		showChildren();
		_playbackSlider->disablePaint(true);
		_volumeController->disablePaint(true);
		_childrenHidden = false;
	}
	start();
	if (_fadeAnimation->animating()) {
		for (const auto child : children()) {
			if (child->isWidgetType()
				&& child != _playbackSlider
				&& child != _volumeController) {
				static_cast<QWidget*>(child)->hide();
			}
		}
		_childrenHidden = true;
	} else {
		fadeFinished();
	}
	_playbackSlider->disablePaint(false);
	_volumeController->disablePaint(false);
}

void Controller::showAnimated() {
	startFading([this]() {
		_fadeAnimation->fadeIn(st::mediaviewShowDuration);
	});
}

void Controller::hideAnimated() {
	startFading([this]() {
		_fadeAnimation->fadeOut(st::mediaviewHideDuration);
	});
}

void Controller::fadeFinished() {
	fadeUpdated(_fadeAnimation->visible() ? 1. : 0.);
}

void Controller::fadeUpdated(float64 opacity) {
	_playbackSlider->setFadeOpacity(opacity);
	_volumeController->setFadeOpacity(opacity);
}

void Controller::updatePlayback(const Player::TrackState &state) {
	updatePlayPauseResumeState(state);
	_playback->updateState(state);
	updateTimeTexts(state);
}

void Controller::updatePlayPauseResumeState(const Player::TrackState &state) {
	auto showPause = (state.state == Player::State::Playing || state.state == Player::State::Resuming || _seekPositionMs >= 0);
	if (showPause != _showPause) {
		disconnect(_playPauseResume, SIGNAL(clicked()), this, _showPause ? SIGNAL(pausePressed()) : SIGNAL(playPressed()));
		_showPause = showPause;
		connect(_playPauseResume, SIGNAL(clicked()), this, _showPause ? SIGNAL(pausePressed()) : SIGNAL(playPressed()));

		_playPauseResume->setIconOverride(_showPause ? &st::mediaviewPauseIcon : nullptr, _showPause ? &st::mediaviewPauseIconOver : nullptr);
	}
}

void Controller::updateTimeTexts(const Player::TrackState &state) {
	qint64 position = 0, length = state.length;

	if (Player::IsStoppedAtEnd(state.state)) {
		position = state.length;
	} else if (!Player::IsStoppedOrStopping(state.state)) {
		position = state.position;
	} else {
		position = 0;
	}
	auto playFrequency = state.frequency;
	auto playAlready = position / playFrequency;
	auto playLeft = (state.length / playFrequency) - playAlready;

	_lastDurationMs = (state.length * 1000LL) / playFrequency;

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
		startFading([this]() {
			_fadeAnimation->refreshCache();
		});
	}
}

void Controller::setInFullScreen(bool inFullScreen) {
	_fullScreenToggle->setIconOverride(inFullScreen ? &st::mediaviewFullScreenOutIcon : nullptr, inFullScreen ? &st::mediaviewFullScreenOutIconOver : nullptr);
	disconnect(_fullScreenToggle, SIGNAL(clicked()), this, SIGNAL(toFullScreenPressed()));
	disconnect(_fullScreenToggle, SIGNAL(clicked()), this, SIGNAL(fromFullScreenPressed()));

	auto handler = inFullScreen ? SIGNAL(fromFullScreenPressed()) : SIGNAL(toFullScreenPressed());
	connect(_fullScreenToggle, SIGNAL(clicked()), this, handler);
}

void Controller::resizeEvent(QResizeEvent *e) {
	int playTop = (height() - _playPauseResume->height()) / 2;
	_playPauseResume->moveToLeft(st::mediaviewPlayPauseLeft, playTop);

	int fullScreenTop = (height() - _fullScreenToggle->height()) / 2;
	_fullScreenToggle->moveToRight(st::mediaviewFullScreenLeft, fullScreenTop);

	_volumeController->resize(st::mediaviewVolumeWidth, st::mediaviewPlayback.seekSize.height());
	_volumeController->moveToRight(st::mediaviewFullScreenLeft + _fullScreenToggle->width() + st::mediaviewVolumeLeft, st::mediaviewPlaybackTop);

	auto playbackWidth = width() - st::mediaviewPlayPauseLeft - _playPauseResume->width() - playTop - fullScreenTop - _volumeController->width() - st::mediaviewVolumeLeft - _fullScreenToggle->width() - st::mediaviewFullScreenLeft;
	_playbackSlider->resize(playbackWidth, st::mediaviewPlayback.seekSize.height());
	_playbackSlider->moveToLeft(st::mediaviewPlayPauseLeft + _playPauseResume->width() + playTop, st::mediaviewPlaybackTop);

	_playedAlready->moveToLeft(st::mediaviewPlayPauseLeft + _playPauseResume->width() + playTop, st::mediaviewPlayProgressTop);
	_toPlayLeft->moveToRight(width() - (st::mediaviewPlayPauseLeft + _playPauseResume->width() + playTop) - playbackWidth, st::mediaviewPlayProgressTop);
}

void Controller::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (_fadeAnimation->paint(p)) {
		return;
	}
	if (_childrenHidden) {
		showChildren();
		_playbackSlider->setFadeOpacity(1.);
		_volumeController->setFadeOpacity(1.);
		_childrenHidden = false;
	}
	App::roundRect(p, rect(), st::mediaviewSaveMsgBg, MediaviewSaveCorners);
}

void Controller::mousePressEvent(QMouseEvent *e) {
	e->accept(); // Don't pass event to the MediaView.
}

Controller::~Controller() = default;

} // namespace Clip
} // namespace Media
