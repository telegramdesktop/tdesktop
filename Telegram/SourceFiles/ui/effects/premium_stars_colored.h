/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/premium_stars.h"

namespace Ui {
class RpWidget;

namespace Premium {

class ColoredMiniStars final {
public:
	ColoredMiniStars(not_null<Ui::RpWidget*> parent);

	void setSize(const QSize &size);
	void setPosition(QPoint position);
	void setColorOverride(std::optional<QColor> color);
	void setCenter(const QRect &rect);
	void paint(Painter &p);

	void setPaused(bool paused);

private:
	Ui::Premium::MiniStars _ministars;
	QRectF _ministarsRect;
	QImage _frame;
	QImage _mask;
	QSize _size;
	QPoint _position;
	std::optional<QColor> _colorOverride;

};

} // namespace Premium
} // namespace Ui
