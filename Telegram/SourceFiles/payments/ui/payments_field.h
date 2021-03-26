/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "base/unique_qptr.h"

namespace Ui {
class RpWidget;
class InputField;
class MaskedInputField;
} // namespace Ui

namespace Payments::Ui {

using namespace ::Ui;

enum class FieldType {
	Text,
	CardNumber,
	CardExpireDate,
	CardCVC,
	Country,
	Phone,
	Email,
};

struct FieldConfig {
	FieldType type = FieldType::Text;
	rpl::producer<QString> placeholder;
	QString value;
	int maxLength = 0;
	bool required = false;
};

class Field final {
public:
	Field(QWidget *parent, FieldConfig &&config);

	[[nodiscard]] RpWidget *widget() const;
	[[nodiscard]] object_ptr<RpWidget> ownedWidget() const;

	[[nodiscard]] QString value() const;

	void setFocus();
	void setFocusFast();
	void showError();

private:
	const FieldType _type = FieldType::Text;
	const base::unique_qptr<RpWidget> _wrap;
	InputField *_input = nullptr;
	MaskedInputField *_masked = nullptr;

};

} // namespace Payments::Ui
