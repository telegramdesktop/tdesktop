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

class AudioMsgId;
struct AudioPlaybackState;

namespace Ui {
class FlatLabel;
class LabelSimple;
class IconButton;
class PlainShadow;
} // namespace Ui

namespace Media {
namespace Clip {
class Playback;
} // namespace Clip

namespace Player {

class PlayButton;
class VolumeWidget;
struct UpdatedEvent;

class Widget : public TWidget, private base::Subscriber {
public:
	Widget(QWidget *parent);

	using CloseCallback = base::lambda<void()>;
	void setCloseCallback(CloseCallback &&callback);

	void setShadowGeometryToLeft(int x, int y, int w, int h);
	void showShadow();
	void hideShadow();

	QPoint getPositionForVolumeWidget() const;
	void volumeWidgetCreated(VolumeWidget *widget);

	~Widget();

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	void leaveEvent(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;

private:
	void handleSeekProgress(float64 progress);
	void handleSeekFinished(float64 progress);

	int getLabelsLeft() const;
	int getLabelsRight() const;
	void updateOverLabelsState(QPoint pos);
	void updateOverLabelsState(bool over);

	void updatePlayPrevNextPositions();
	void updateLabelsGeometry();
	void updateRepeatTrackIcon();
	void createPrevNextButtons();
	void destroyPrevNextButtons();

	void updateVolumeToggleIcon();

	void handleSongUpdate(const UpdatedEvent &e);
	void handleSongChange();
	void handlePlaylistUpdate();

	void updateTimeText(const AudioMsgId &audioId, const AudioPlaybackState &playbackState);
	void updateTimeLabel();

	TimeMs _seekPositionMs = -1;
	TimeMs _lastDurationMs = 0;
	QString _time;

	class PlayButton;
	object_ptr<Ui::FlatLabel> _nameLabel;
	object_ptr<Ui::LabelSimple> _timeLabel;
	object_ptr<Ui::IconButton> _previousTrack = { nullptr };
	object_ptr<PlayButton> _playPause;
	object_ptr<Ui::IconButton> _nextTrack = { nullptr };
	object_ptr<Ui::IconButton> _volumeToggle;
	object_ptr<Ui::IconButton> _repeatTrack;
	object_ptr<Ui::IconButton> _close;
	object_ptr<Ui::PlainShadow> _shadow = { nullptr };
	std_::unique_ptr<Clip::Playback> _playback;

};

} // namespace Clip
} // namespace Media
