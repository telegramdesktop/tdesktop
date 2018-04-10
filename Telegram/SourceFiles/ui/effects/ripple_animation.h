/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "styles/style_widgets.h"

namespace Ui {

class RippleAnimation {
public:
	using UpdateCallback = base::lambda<void()>;

	// White upon transparent mask, like colorizeImage(black-white-mask, white).
	RippleAnimation(const style::RippleAnimation &st, QImage mask, const UpdateCallback &update);

	void add(QPoint origin, int startRadius = 0);
	void addFading();
	void lastStop();
	void lastUnstop();
	void lastFinish();

	void paint(QPainter &p, int x, int y, int outerWidth, TimeMs ms, const QColor *colorOverride = nullptr);

	bool empty() const {
		return _ripples.isEmpty();
	}

	static QImage maskByDrawer(QSize size, bool filled, base::lambda<void(QPainter &p)> drawer);
	static QImage rectMask(QSize size);
	static QImage roundRectMask(QSize size, int radius);
	static QImage ellipseMask(QSize size);

	~RippleAnimation() {
		clear();
	}

private:
	void clear();
	void clearFinished();

	const style::RippleAnimation &_st;
	QPixmap _mask;
	UpdateCallback _update;

	class Ripple;
	QList<Ripple*> _ripples;

};

} // namespace Ui
