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
	if (ready) {
		_checkbox.create(this, object_ptr<Ui::IconButton>(
			this,
			st::passportRowCheckbox));
		_checkbox->show(anim::type::instant);
		_checkbox->entity()->addClickHandler([=] {
			_checkbox->hide(anim::type::normal);
		});
	} else {
		_checkbox.destroy();
	}
	resizeToWidth(width());
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
	if (_checkbox) {
		const auto right = st::passportRowPadding.right();
		_checkbox->moveToRight(
			right,
			(result - _checkbox->height()) / 2,
			newWidth);
	}
	return result;
}

int FormRow::countAvailableWidth(int newWidth) const {
	return newWidth
		- st::passportRowPadding.left()
		- st::passportRowPadding.right()
		- (_checkbox ? _checkbox->width() : 0);
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

	_title.drawLeft(p, left, top, availableWidth, width());
	top += _titleHeight + st::passportRowSkip;

	_description.drawLeft(p, left, top, availableWidth, width());
	top += _descriptionHeight + st::passportRowPadding.bottom();
}

} // namespace Passport
