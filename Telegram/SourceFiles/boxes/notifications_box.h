/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
