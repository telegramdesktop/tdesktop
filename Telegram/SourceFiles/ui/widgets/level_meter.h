/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rp_widget.h"

namespace style {
struct LevelMeter;
} // namespace style

namespace Ui {

class LevelMeter : public RpWidget {
public:
	LevelMeter(QWidget *parent, const style::LevelMeter &st);

	void setValue(float value);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const style::LevelMeter &_st;
	float _value = 0.0f;

};

} // namespace Ui
