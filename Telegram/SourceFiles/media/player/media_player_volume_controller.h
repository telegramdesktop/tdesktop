/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/rp_widget.h"
#include "base/object_ptr.h"

#include <QtCore/QTimer>

namespace Ui {
class IconButton;
class MediaSlider;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Media {
namespace Player {

class VolumeController : public Ui::RpWidget, private base::Subscriber {
public:
	VolumeController(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	void setIsVertical(bool vertical);

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	void setVolume(float64 volume);
	void applyVolumeChange(float64 volume);

	object_ptr<Ui::MediaSlider> _slider;

};

class VolumeWidget : public Ui::RpWidget {
	Q_OBJECT

public:
	VolumeWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	bool overlaps(const QRect &globalRect);

	QMargins getMargin() const;

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	bool eventFilter(QObject *obj, QEvent *e) override;

private Q_SLOTS:
	void onShowStart();
	void onHideStart();

private:
	void otherEnter();
	void otherLeave();

	void appearanceCallback();
	void hidingFinished();
	void startAnimation();

	bool _hiding = false;

	QPixmap _cache;
	Ui::Animations::Simple _a_appearance;

	QTimer _hideTimer, _showTimer;

	object_ptr<VolumeController> _controller;

};

} // namespace Player
} // namespace Media
