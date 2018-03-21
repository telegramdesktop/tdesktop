/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "passport/passport_edit_identity_box.h"

#include "passport/passport_form_controller.h"
#include "ui/widgets/input_fields.h"
#include "lang/lang_keys.h"
#include "styles/style_widgets.h"
#include "styles/style_boxes.h"
#include "styles/style_passport.h"

namespace Passport {

IdentityBox::IdentityBox(
	QWidget*,
	not_null<FormController*> controller,
	int fieldIndex,
	const IdentityData &data)
: _controller(controller)
, _fieldIndex(fieldIndex)
, _name(
	this,
	st::defaultInputField,
	langFactory(lng_signup_firstname),
	data.name)
, _surname(
	this,
	st::defaultInputField,
	langFactory(lng_signup_lastname),
	data.surname) {
}

void IdentityBox::prepare() {
	setTitle(langFactory(lng_passport_identity_title));

	addButton(langFactory(lng_settings_save), [=] {
		save();
	});
	addButton(langFactory(lng_cancel), [=] {
		closeBox();
	});

	setDimensions(
		st::boxWideWidth,
		(st::contactPadding.top()
			+ _name->height()
			+ st::contactSkip
			+ _surname->height()
			+ st::contactPadding.bottom()
			+ st::boxPadding.bottom()));
}

void IdentityBox::setInnerFocus() {
	_name->setFocusFast();
}

void IdentityBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	_name->resize((width()
			- st::boxPadding.left()
			- st::boxPadding.right()),
		_name->height());
	_surname->resize(_name->width(), _surname->height());
	_name->moveToLeft(
		st::boxPadding.left(),
		st::contactPadding.top());
	_surname->moveToLeft(
		st::boxPadding.left(),
		_name->y() + _name->height() + st::contactSkip);
}

void IdentityBox::save() {
	auto data = IdentityData();
	data.name = _name->getLastText();
	data.surname = _surname->getLastText();
	_controller->saveFieldIdentity(_fieldIndex, data);
}

} // namespace Passport
