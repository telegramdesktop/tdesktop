/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/rect_part.h"
#include "ui/style/style_core.h"

enum class ImageRoundRadius;
class QPainter;

namespace Ui {

void DrawRoundedRect(
	QPainter &p,
	const QRect &rect,
	const QBrush &brush,
	const std::array<QImage, 4> & corners,
	RectParts parts = RectPart::Full);

class RoundRect final {
public:
	RoundRect(ImageRoundRadius radius, const style::color &color);

	void paint(QPainter &p, const QRect &rect) const;

private:
	style::color _color;
	std::array<QImage, 4> _corners;

	rpl::lifetime _lifetime;

};

} // namespace Ui
