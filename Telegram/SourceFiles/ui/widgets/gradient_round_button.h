/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/buttons.h"

namespace Ui {

class GradientButton final : public Ui::RippleButton {
public:
	GradientButton(QWidget *widget, QGradientStops stops);

	void startGlareAnimation();
	void setGlarePaused(bool paused);

private:
	void paintEvent(QPaintEvent *e);
	void paintGlare(QPainter &p);
	void validateBg();
	void validateGlare();

	struct Glare final {
		crl::time birthTime = 0;
		crl::time deathTime = 0;
	};

	QGradientStops _stops;
	QImage _bg;

	struct {
		Ui::Animations::Basic animation;
		Glare glare;
		QPixmap pixmap;
		int width = 0;
		bool paused = false;
	} _glare;

};

} // namespace Ui
