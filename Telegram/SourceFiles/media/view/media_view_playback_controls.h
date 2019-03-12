/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace Ui {
class LabelSimple;
class FadeAnimation;
class IconButton;
class MediaSlider;
} // namespace Ui

namespace Media {
namespace Player {
struct TrackState;
} // namespace Player

namespace View {

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
		virtual void playbackControlsToFullScreen() = 0;
		virtual void playbackControlsFromFullScreen() = 0;
	};

	PlaybackControls(QWidget *parent, not_null<Delegate*> delegate);

	void showAnimated();
	void hideAnimated();

	void updatePlayback(const Player::TrackState &state);
	void setInFullScreen(bool inFullScreen);

	~PlaybackControls();

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

	void updatePlayPauseResumeState(const Player::TrackState &state);
	void updateTimeTexts(const Player::TrackState &state);
	void refreshTimeTexts();

	not_null<Delegate*> _delegate;

	bool _inFullScreen = false;
	bool _showPause = false;
	bool _childrenHidden = false;
	QString _timeAlready, _timeLeft;
	crl::time _seekPositionMs = -1;
	crl::time _lastDurationMs = 0;

	object_ptr<Ui::IconButton> _playPauseResume;
	object_ptr<Ui::MediaSlider> _playbackSlider;
	std::unique_ptr<PlaybackProgress> _playbackProgress;
	std::unique_ptr<PlaybackProgress> _receivedTillProgress;
	object_ptr<Ui::MediaSlider> _volumeController;
	object_ptr<Ui::IconButton> _fullScreenToggle;
	object_ptr<Ui::LabelSimple> _playedAlready;
	object_ptr<Ui::LabelSimple> _toPlayLeft;

	std::unique_ptr<Ui::FadeAnimation> _fadeAnimation;

};

} // namespace View
} // namespace Media
