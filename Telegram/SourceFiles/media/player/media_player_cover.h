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

class FlatLabel;
namespace Ui {
class LabelSimple;
class IconButton;
} // namespace Ui

namespace Media {
namespace Player {

class PlaybackWidget;
class VolumeController;

class CoverWidget : public TWidget {
public:
	CoverWidget(QWidget *parent);

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void updatePlayPrevNextPositions();

	ChildWidget<FlatLabel> _nameLabel;
	ChildWidget<Ui::LabelSimple> _timeLabel;
	ChildWidget<PlaybackWidget> _playback;
	ChildWidget<Ui::IconButton> _previousTrack = { nullptr };
	ChildWidget<Ui::IconButton> _playPause;
	ChildWidget<Ui::IconButton> _nextTrack = { nullptr };
	ChildWidget<VolumeController> _volumeController;
	ChildWidget<Ui::IconButton> _repeatTrack;

};

} // namespace Clip
} // namespace Media
