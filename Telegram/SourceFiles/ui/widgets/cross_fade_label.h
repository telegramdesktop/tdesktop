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
struct LabelSimple;
} // namespace style

namespace Ui {

class CrossFadeLabel final : public RpWidget {
public:
	CrossFadeLabel(
		QWidget *parent,
		const style::LabelSimple &st);

	void setText(const QString &text, anim::type animated = anim::type::normal);
	void setDirection(int direction);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const style::LabelSimple &_st;

	QString _current;
	QString _previous;
	int _currentWidth = 0;
	int _previousWidth = 0;
	int _direction = 1;

	Animations::Simple _animation;

};

} // namespace Ui
