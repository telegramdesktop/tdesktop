/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_form_row.h"

#include "ui/wrap/fade_wrap.h"
#include "ui/text_options.h"
#include "styles/style_boxes.h"
#include "styles/style_passport.h"

namespace Passport {

FormRow::FormRow(
	QWidget *parent,
	const QString &title,
	const QString &description)
: RippleButton(parent, st::passportRowRipple)
, _title(
	st::semiboldTextStyle,
	title,
	Ui::NameTextOptions(),
	st::boxWideWidth / 2)
, _description(
	st::defaultTextStyle,
	description,
	Ui::NameTextOptions(),
	st::boxWideWidth / 2) {
}

void FormRow::setReady(bool ready) {
	_ready = ready;
	resizeToWidth(width());
	update();
}

int FormRow::resizeGetHeight(int newWidth) {
	const auto availableWidth = countAvailableWidth(newWidth);
	_titleHeight = _title.countHeight(availableWidth);
	_descriptionHeight = _description.countHeight(availableWidth);
	const auto result = st::passportRowPadding.top()
		+ _titleHeight
		+ st::passportRowSkip
		+ _descriptionHeight
		+ st::passportRowPadding.bottom();
	return result;
}

int FormRow::countAvailableWidth(int newWidth) const {
	return newWidth
		- st::passportRowPadding.left()
		- st::passportRowPadding.right()
		- (_ready ? st::passportRowReadyIcon : st::passportRowEmptyIcon).width();
}

int FormRow::countAvailableWidth() const {
	return countAvailableWidth(width());
}

void FormRow::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto ms = getms();
	paintRipple(p, 0, 0, ms);

	const auto left = st::passportRowPadding.left();
	const auto availableWidth = countAvailableWidth();
	auto top = st::passportRowPadding.top();

	p.setPen(st::passportRowTitleFg);
	_title.drawLeft(p, left, top, availableWidth, width());
	top += _titleHeight + st::passportRowSkip;

	p.setPen(st::passportRowDescriptionFg);
	_description.drawLeft(p, left, top, availableWidth, width());
	top += _descriptionHeight + st::passportRowPadding.bottom();

	const auto &icon = _ready
		? st::passportRowReadyIcon
		: st::passportRowEmptyIcon;
	icon.paint(
		p,
		width() - st::passportRowPadding.right() - icon.width(),
		(height() - icon.height()) / 2,
		width());
}

} // namespace Passport
