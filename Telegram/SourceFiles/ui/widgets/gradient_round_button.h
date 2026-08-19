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

	void setStops(QGradientStops stops);
	void setFullRadius(bool enabled);

	void startGlareAnimation();
	void setGlarePaused(bool paused);

private:
	void paintEvent(QPaintEvent *e) override;
	void paintGlare(QPainter &p);
	void validateBg();
	void validateGlare();

	QImage prepareRippleMask() const override;

	QGradientStops _stops;
	QImage _bg;

	GlareEffect _glare;
	bool _fullRadius = false;

};

} // namespace Ui
