/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
