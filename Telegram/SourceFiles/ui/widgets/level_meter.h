/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "styles/style_widgets.h"
#include "ui/rp_widget.h"

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
