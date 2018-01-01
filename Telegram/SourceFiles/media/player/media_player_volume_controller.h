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
class IconButton;
class MediaSlider;
} // namespace Ui

namespace Media {
namespace Player {

class VolumeController : public TWidget, private base::Subscriber {
public:
	VolumeController(QWidget *parent);

	void setIsVertical(bool vertical);

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	void setVolume(float64 volume);
	void applyVolumeChange(float64 volume);

	object_ptr<Ui::MediaSlider> _slider;

};

class VolumeWidget : public TWidget {
	Q_OBJECT

public:
	VolumeWidget(QWidget *parent);

	bool overlaps(const QRect &globalRect);

	QMargins getMargin() const;

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	bool eventFilter(QObject *obj, QEvent *e) override;

private slots:
	void onShowStart();
	void onHideStart();
	void onWindowActiveChanged();

private:
	void otherEnter();
	void otherLeave();

	void appearanceCallback();
	void hidingFinished();
	void startAnimation();

	bool _hiding = false;

	QPixmap _cache;
	Animation _a_appearance;

	QTimer _hideTimer, _showTimer;

	object_ptr<VolumeController> _controller;

};

} // namespace Clip
} // namespace Media
