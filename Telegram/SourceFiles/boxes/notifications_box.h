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

#include "boxes/abstract_box.h"

namespace Ui {
class LinkButton;
class SettingsSlider;
} // namespace Ui

class NotificationsBox : public BoxContent {
public:
	NotificationsBox(QWidget*);
	~NotificationsBox();

protected:
	void prepare() override;

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	using ScreenCorner = Notify::ScreenCorner;
	void countChanged();
	void setOverCorner(ScreenCorner corner);
	void clearOverCorner();

	class SampleWidget;
	void removeSample(SampleWidget *widget);

	int currentCount() const;

	QRect getScreenRect() const;
	int getContentLeft() const;
	void prepareNotificationSampleSmall();
	void prepareNotificationSampleLarge();
	void prepareNotificationSampleUserpic();

	QPixmap _notificationSampleUserpic;
	QPixmap _notificationSampleSmall;
	QPixmap _notificationSampleLarge;
	ScreenCorner _chosenCorner;
	std::vector<Animation> _sampleOpacities;

	bool _isOverCorner = false;
	ScreenCorner _overCorner = ScreenCorner::TopLeft;
	bool _isDownCorner = false;
	ScreenCorner _downCorner = ScreenCorner::TopLeft;

	int _oldCount;
	object_ptr<Ui::SettingsSlider> _countSlider;

	QVector<SampleWidget*> _cornerSamples[4];

};
