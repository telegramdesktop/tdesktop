/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Media {
namespace Clip {

class VolumeController : public TWidget {
	Q_OBJECT

public:
	VolumeController(QWidget *parent);

	void setVolume(float64 volume);

signals:
	void volumeChanged(float64 volume);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	void setOver(bool over);
	void changeVolume(float64 newVolume);

	float64 _volume = 0.;
	int _downCoord = -1; // < 0 means mouse is not pressed

	bool _over = false;
	Animation _a_over;

};

} // namespace Clip
} // namespace Media
