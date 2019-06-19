/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_playback_controls.h"

#include "media/audio/media_audio.h"
#include "media/view/media_view_playback_progress.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/effects/fade_animation.h"
#include "ui/widgets/buttons.h"
#include "lang/lang_keys.h"
#include "layout.h"
#include "styles/style_mediaview.h"

namespace Media {
namespace View {

PlaybackControls::PlaybackControls(
	QWidget *parent,
	not_null<Delegate*> delegate)
: RpWidget(parent)
, _delegate(delegate)
, _playPauseResume(this, st::mediaviewPlayButton)
, _playbackSlider(this, st::mediaviewPlayback)
, _playbackProgress(std::make_unique<PlaybackProgress>())
, _volumeController(this, st::mediaviewPlayback)
, _fullScreenToggle(this, st::mediaviewFullScreenButton)
, _playedAlready(this, st::mediaviewPlayProgressLabel)
, _toPlayLeft(this, st::mediaviewPlayProgressLabel)
, _fadeAnimation(std::make_unique<Ui::FadeAnimation>(this)) {
	_fadeAnimation->show();
	_fadeAnimation->setFinishedCallback([=] {
		fadeFinished();
	});
	_fadeAnimation->setUpdatedCallback([=](float64 opacity) {
		fadeUpdated(opacity);
	});

	_volumeController->setValue(_delegate->playbackControlsCurrentVolume());
	_volumeController->setChangeProgressCallback([=](float64 value) {
		_delegate->playbackControlsVolumeChanged(value);
	});

	_playPauseResume->addClickHandler([=] {
		if (_showPause) {
			_delegate->playbackControlsPause();
		} else {
			_delegate->playbackControlsPlay();
		}
	});
	_fullScreenToggle->addClickHandler([=] {
		if (_inFullScreen) {
			_delegate->playbackControlsFromFullScreen();
		} else {
			_delegate->playbackControlsToFullScreen();
		}
	});

	_playbackProgress->setValueChangedCallback([=](
			float64 value,
			float64 receivedTill) {
		_playbackSlider->setValue(value, receivedTill);
	});
	_playbackSlider->setChangeProgressCallback([=](float64 value) {
		_playbackProgress->setValue(value, false);

		// This may destroy PlaybackControls.
		handleSeekProgress(value);
	});
	_playbackSlider->setChangeFinishedCallback([=](float64 value) {
		_playbackProgress->setValue(value, false);
		handleSeekFinished(value);
	});
}

void PlaybackControls::handleSeekProgress(float64 progress) {
	if (!_lastDurationMs) return;

	const auto positionMs = snap(
		static_cast<crl::time>(progress * _lastDurationMs),
		crl::time(0),
		_lastDurationMs);
	if (_seekPositionMs != positionMs) {
		_seekPositionMs = positionMs;
		refreshTimeTexts();

		// This may destroy PlaybackControls.
		_delegate->playbackControlsSeekProgress(positionMs);
	}
}

void PlaybackControls::handleSeekFinished(float64 progress) {
	if (!_lastDurationMs) return;

	const auto positionMs = snap(
		static_cast<crl::time>(progress * _lastDurationMs),
		crl::time(0),
		_lastDurationMs);
	_seekPositionMs = -1;
	_delegate->playbackControlsSeekFinished(positionMs);
	refreshTimeTexts();
}

template <typename Callback>
void PlaybackControls::startFading(Callback start) {
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

void PlaybackControls::showAnimated() {
	startFading([this]() {
		_fadeAnimation->fadeIn(st::mediaviewShowDuration);
	});
}

void PlaybackControls::hideAnimated() {
	startFading([this]() {
		_fadeAnimation->fadeOut(st::mediaviewHideDuration);
	});
}

void PlaybackControls::fadeFinished() {
	fadeUpdated(_fadeAnimation->visible() ? 1. : 0.);
}

void PlaybackControls::fadeUpdated(float64 opacity) {
	_playbackSlider->setFadeOpacity(opacity);
	_volumeController->setFadeOpacity(opacity);
}

void PlaybackControls::updatePlayback(const Player::TrackState &state) {
	updatePlayPauseResumeState(state);
	_playbackProgress->updateState(state, countDownloadedTillPercent(state));
	updateTimeTexts(state);
}

float64 PlaybackControls::countDownloadedTillPercent(
		const Player::TrackState &state) const {
	if (_loadingReady > 0 && _loadingReady == _loadingTotal) {
		return 1.;
	}
	const auto header = state.fileHeaderSize;
	if (!header || _loadingReady <= header || _loadingTotal <= header) {
		return 0.;
	}
	return (_loadingReady - header) / float64(_loadingTotal - header);
}

void PlaybackControls::setLoadingProgress(int ready, int total) {
	if (_loadingReady == ready && _loadingTotal == total) {
		return;
	}
	_loadingReady = ready;
	_loadingTotal = total;
	if (_loadingReady != 0 && _loadingReady != _loadingTotal) {
		if (!_downloadProgress) {
			_downloadProgress.create(this, st::mediaviewPlayProgressLabel);
			_downloadProgress->setVisible(!_fadeAnimation->animating());
			_loadingPercent = -1;
		}
		const auto progress = total ? (ready / float64(total)) : 0.;
		const auto percent = int(std::round(progress * 100));
		if (_loadingPercent != percent) {
			_loadingPercent = percent;
			_downloadProgress->setText(tr::lng_mediaview_video_loading(
				tr::now,
				lt_percent,
				QString::number(percent) + '%'));
			if (_playbackSlider->width() > _downloadProgress->width()) {
				const auto left = (_playbackSlider->width() - _downloadProgress->width()) / 2;
				_downloadProgress->move(_playbackSlider->x() + left, st::mediaviewPlayProgressTop);
			}
			refreshFadeCache();
		}
	} else {
		_downloadProgress.destroy();
	}
}

void PlaybackControls::refreshFadeCache() {
	if (!_fadeAnimation->animating()) {
		return;
	}
	startFading([&] {
		_fadeAnimation->refreshCache();
	});
}

void PlaybackControls::updatePlayPauseResumeState(const Player::TrackState &state) {
	auto showPause = ShowPauseIcon(state.state) || (_seekPositionMs >= 0);
	if (showPause != _showPause) {
		_showPause = showPause;
		_playPauseResume->setIconOverride(_showPause ? &st::mediaviewPauseIcon : nullptr, _showPause ? &st::mediaviewPauseIconOver : nullptr);
	}
}

void PlaybackControls::updateTimeTexts(const Player::TrackState &state) {
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

	_lastDurationMs = (state.length * crl::time(1000)) / playFrequency;

	_timeAlready = formatDurationText(playAlready);
	auto minus = QChar(8722);
	_timeLeft = minus + formatDurationText(playLeft);

	if (_seekPositionMs < 0) {
		refreshTimeTexts();
	}
}

void PlaybackControls::refreshTimeTexts() {
	auto alreadyChanged = false, leftChanged = false;
	auto timeAlready = _timeAlready;
	auto timeLeft = _timeLeft;
	if (_seekPositionMs >= 0) {
		auto playAlready = _seekPositionMs / crl::time(1000);
		auto playLeft = (_lastDurationMs / crl::time(1000)) - playAlready;

		timeAlready = formatDurationText(playAlready);
		auto minus = QChar(8722);
		timeLeft = minus + formatDurationText(playLeft);
	}

	_playedAlready->setText(timeAlready, &alreadyChanged);
	_toPlayLeft->setText(timeLeft, &leftChanged);
	if (alreadyChanged || leftChanged) {
		resizeEvent(nullptr);
		refreshFadeCache();
	}
}

void PlaybackControls::setInFullScreen(bool inFullScreen) {
	if (_inFullScreen != inFullScreen) {
		_inFullScreen = inFullScreen;
		_fullScreenToggle->setIconOverride(
			_inFullScreen ? &st::mediaviewFullScreenOutIcon : nullptr,
			_inFullScreen ? &st::mediaviewFullScreenOutIconOver : nullptr);
	}
}

void PlaybackControls::resizeEvent(QResizeEvent *e) {
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

	if (_downloadProgress) {
		const auto left = (_playbackSlider->width() - _downloadProgress->width()) / 2;
		_downloadProgress->move(_playbackSlider->x() + left, st::mediaviewPlayProgressTop);
	}
}

void PlaybackControls::paintEvent(QPaintEvent *e) {
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

void PlaybackControls::mousePressEvent(QMouseEvent *e) {
	e->accept(); // Don't pass event to the Media::View::OverlayWidget.
}

PlaybackControls::~PlaybackControls() = default;

} // namespace View
} // namespace Media
