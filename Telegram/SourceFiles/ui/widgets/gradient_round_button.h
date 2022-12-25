/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/buttons.h"
#include "ui/effects/glare.h"

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

	QGradientStops _stops;
	QImage _bg;

	GlareEffect _glare;

};

} // namespace Ui
