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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
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

	void grabStart() override;
	void grabFinish() override;

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
