/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"
#include "base/object_ptr.h"
#include "media/media_common.h"

namespace Ui {
class LabelSimple;
class FadeAnimation;
class IconButton;
class MediaSlider;
class PopupMenu;
} // namespace Ui

namespace Media::Player {
struct TrackState;
class SettingsButton;
class SpeedController;
} // namespace Media::Player

namespace Media::View {

class PlaybackProgress;

class PlaybackControls : public Ui::RpWidget {
public:
	class Delegate {
	public:
		virtual void playbackControlsPlay() = 0;
		virtual void playbackControlsPause() = 0;
		virtual void playbackControlsSeekProgress(crl::time position) = 0;
		virtual void playbackControlsSeekFinished(crl::time position) = 0;
		virtual void playbackControlsVolumeChanged(float64 volume) = 0;
		[[nodiscard]] virtual float64 playbackControlsCurrentVolume() = 0;
		virtual void playbackControlsVolumeToggled() = 0;
		virtual void playbackControlsVolumeChangeFinished() = 0;
		virtual void playbackControlsSpeedChanged(float64 speed) = 0;
		[[nodiscard]] virtual float64 playbackControlsCurrentSpeed(
			bool lastNonDefault) = 0;
		[[nodiscard]] virtual auto playbackControlsQualities()
			-> std::vector<int> = 0;
		[[nodiscard]] virtual auto playbackControlsCurrentQuality()
			-> VideoQuality = 0;
		virtual void playbackControlsQualityChanged(int quality) = 0;
		virtual void playbackControlsToFullScreen() = 0;
		virtual void playbackControlsFromFullScreen() = 0;
		virtual void playbackControlsToPictureInPicture() = 0;
		virtual void playbackControlsRotate() = 0;
	};

	PlaybackControls(QWidget *parent, not_null<Delegate*> delegate);
	~PlaybackControls();

	void showAnimated();
	void hideAnimated();

	void updatePlayback(const Player::TrackState &state);
	void setLoadingProgress(int64 ready, int64 total);
	void setInFullScreen(bool inFullScreen);
	[[nodiscard]] bool hasMenu() const;
	[[nodiscard]] bool dragging() const;

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

private:
	void handleSeekProgress(float64 progress);
	void handleSeekFinished(float64 progress);

	template <typename Callback>
	void startFading(Callback start);
	void fadeFinished();
	void fadeUpdated(float64 opacity);
	void refreshFadeCache();
	[[nodiscard]] float64 countDownloadedTillPercent(
		const Player::TrackState &state) const;

	void updatePlaybackSpeed(float64 speed);
	void updateVolumeToggleIcon();
	void updateDownloadProgressPosition();

	void updatePlayPauseResumeState(const Player::TrackState &state);
	void updateTimeTexts(const Player::TrackState &state);
	void refreshTimeTexts();

	[[nodiscard]] float64 speedLookup(bool lastNonDefault) const;
	void saveSpeed(float64 speed);

	void saveQuality(int quality);
	void updateSpeedToggleQuality();

	const not_null<Delegate*> _delegate;

	bool _speedControllable = false;
	std::vector<int> _qualitiesList;

	bool _inFullScreen = false;
	bool _showPause = false;
	bool _childrenHidden = false;
	QString _timeAlready, _timeLeft;
	crl::time _seekPositionMs = -1;
	crl::time _lastDurationMs = 0;
	int64 _loadingReady = 0;
	int64 _loadingTotal = 0;
	int _loadingPercent = 0;

	object_ptr<Ui::IconButton> _playPauseResume;
	object_ptr<Ui::MediaSlider> _playbackSlider;
	std::unique_ptr<PlaybackProgress> _playbackProgress;
	std::unique_ptr<PlaybackProgress> _receivedTillProgress;
	object_ptr<Ui::IconButton> _volumeToggle;
	object_ptr<Ui::MediaSlider> _volumeController;
	object_ptr<Player::SettingsButton> _speedToggle;
	object_ptr<Ui::IconButton> _fullScreenToggle;
	object_ptr<Ui::IconButton> _pictureInPicture;
	object_ptr<Ui::LabelSimple> _playedAlready;
	object_ptr<Ui::LabelSimple> _toPlayLeft;
	object_ptr<Ui::LabelSimple> _downloadProgress = { nullptr };
	std::unique_ptr<Player::SpeedController> _speedController;
	std::unique_ptr<Ui::FadeAnimation> _fadeAnimation;

};

} // namespace Media::View
