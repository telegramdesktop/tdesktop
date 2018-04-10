/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/style/style_core_color.h"

namespace style {
namespace internal {

Color::Proxy Color::operator[](const style::palette &paletteOverride) const {
	auto index = main_palette::indexOfColor(*this);
	return Proxy((index >= 0) ? paletteOverride.colorAtIndex(index) : (*this));
}

ColorData::ColorData(uchar r, uchar g, uchar b, uchar a) : c(int(r), int(g), int(b), int(a)), p(c), b(c) {
}

void ColorData::set(uchar r, uchar g, uchar b, uchar a) {
	this->c = QColor(int(r), int(g), int(b), int(a));
	this->p = QPen(c);
	this->b = QBrush(c);
}

} // namespace internal
} // namespace style
