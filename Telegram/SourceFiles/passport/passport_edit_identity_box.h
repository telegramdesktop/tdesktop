/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"

namespace Ui {
class InputField;
} // namespace Ui

namespace Passport {

class FormController;

struct IdentityData {
	QString name;
	QString surname;
};

class IdentityBox : public BoxContent {
public:
	IdentityBox(
		QWidget*,
		not_null<FormController*> controller,
		int fieldIndex,
		const IdentityData &data);

protected:
	void prepare() override;
	void setInnerFocus() override;

	void resizeEvent(QResizeEvent *e) override;

private:
	void save();

	not_null<FormController*> _controller;
	int _fieldIndex = -1;

	object_ptr<Ui::InputField> _name;
	object_ptr<Ui::InputField> _surname;

};

} // namespace Passport
