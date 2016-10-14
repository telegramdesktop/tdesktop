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
#pragma once

class AudioMsgId;
struct AudioPlaybackState;
class FlatLabel;

namespace Ui {
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

	using CloseCallback = base::lambda_unique<void()>;
	void setCloseCallback(CloseCallback &&callback);

	void setShadowGeometryToLeft(int x, int y, int w, int h);
	void showShadow();
	void hideShadow();

	QPoint getPositionForVolumeWidget() const;
	void volumeWidgetCreated(VolumeWidget *widget);

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

	int64 _seekPositionMs = -1;
	int64 _lastDurationMs = 0;
	QString _time;
	bool _mouseOverLabels = false;

	class PlayButton;
	ChildWidget<FlatLabel> _nameLabel;
	ChildWidget<Ui::LabelSimple> _timeLabel;
	ChildWidget<Ui::IconButton> _previousTrack = { nullptr };
	ChildWidget<PlayButton> _playPause;
	ChildWidget<Ui::IconButton> _nextTrack = { nullptr };
	ChildWidget<Ui::IconButton> _volumeToggle;
	ChildWidget<Ui::IconButton> _repeatTrack;
	ChildWidget<Ui::IconButton> _close;
	ChildWidget<Ui::PlainShadow> _shadow = { nullptr };
	ChildWidget<Clip::Playback> _playback;

};

} // namespace Clip
} // namespace Media
