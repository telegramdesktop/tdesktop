/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_panel_details_row.h"

#include "ui/widgets/input_fields.h"
#include "styles/style_passport.h"

namespace Passport {

PanelDetailsRow::PanelDetailsRow(
	QWidget *parent,
	const QString &label,
	const QString &value)
: _label(label)
, _field(this, st::passportDetailsField, nullptr, value) {
}

bool PanelDetailsRow::setFocusFast() {
	_field->setFocusFast();
	return true;
}

QString PanelDetailsRow::getValue() const {
	return _field->getLastText();
}

int PanelDetailsRow::resizeGetHeight(int newWidth) {
	const auto padding = st::passportDetailsPadding;
	const auto inputLeft = padding.left() + st::passportDetailsFieldLeft;
	const auto inputTop = st::passportDetailsFieldTop;
	const auto inputRight = padding.right();
	const auto inputWidth = std::max(newWidth - inputLeft - inputRight, 0);
	_field->setGeometry(inputLeft, inputTop, inputWidth, _field->height());
	return padding.top() + st::semiboldFont->height + padding.bottom();
}

void PanelDetailsRow::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.setFont(st::semiboldFont);
	p.setPen(st::passportDetailsField.placeholderFg);
	const auto padding = st::passportDetailsPadding;
	p.drawTextLeft(padding.left(), padding.top(), width(), _label);
}

} // namespace Passport
