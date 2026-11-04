/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

namespace style {
struct LevelMeter;
} // namespace style

namespace Ui {

class LevelMeter : public RpWidget {
public:
	LevelMeter(QWidget *parent, const style::LevelMeter &st);

	void animateTo(float value, crl::time duration);
	void stopAnimation();

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	bool animationCallback(crl::time now);

	const style::LevelMeter &_st;
	anim::value _value;
	crl::time _duration = 0;
	Animations::Basic _animation;

};

} // namespace Ui
