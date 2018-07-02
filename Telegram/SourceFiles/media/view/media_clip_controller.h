/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

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

namespace Clip {

class Playback;
class VolumeController;

class Controller : public TWidget {
	Q_OBJECT

public:
	Controller(QWidget *parent);

	void showAnimated();
	void hideAnimated();

	void updatePlayback(const Player::TrackState &state);
	void setInFullScreen(bool inFullScreen);

	~Controller();

signals:
	void playPressed();
	void pausePressed();
	void seekProgress(TimeMs positionMs);
	void seekFinished(TimeMs positionMs);
	void volumeChanged(float64 volume);
	void toFullScreenPressed();
	void fromFullScreenPressed();

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

	bool _showPause = false;
	bool _childrenHidden = false;
	QString _timeAlready, _timeLeft;
	TimeMs _seekPositionMs = -1;
	TimeMs _lastDurationMs = 0;

	object_ptr<Ui::IconButton> _playPauseResume;
	object_ptr<Ui::MediaSlider> _playbackSlider;
	std::unique_ptr<Playback> _playback;
	object_ptr<VolumeController> _volumeController;
	object_ptr<Ui::IconButton> _fullScreenToggle;
	object_ptr<Ui::LabelSimple> _playedAlready;
	object_ptr<Ui::LabelSimple> _toPlayLeft;

	std::unique_ptr<Ui::FadeAnimation> _fadeAnimation;

};

} // namespace Clip
} // namespace Media
