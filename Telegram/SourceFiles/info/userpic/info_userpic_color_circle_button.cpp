/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#include "info/userpic/info_userpic_color_circle_button.h"

#include "settings/settings_chat.h" // Settings::PaintRoundColorButton.
#include "ui/painter.h"

namespace UserpicBuilder {

void CircleButton::setIndex(int index) {
	_index = index;
}

int CircleButton::index() const {
	return _index;
}

void CircleButton::setBrush(QBrush brush) {
	_brush = brush;
	update();
}

void CircleButton::setSelectedProgress(float64 progress) {
	if (_selectedProgress != progress) {
		_selectedProgress = progress;
		update();
	}
}

void CircleButton::paintEvent(QPaintEvent *event) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto h = height();
	Settings::PaintRoundColorButton(p, h, _brush, _selectedProgress);
}

} // namespace UserpicBuilder
