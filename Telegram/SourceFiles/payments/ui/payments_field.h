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
class BoxContent;
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
	Fn<void(object_ptr<BoxContent>)> showBox;
	QString defaultCountry;
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
	void setupMaskedGeometry();
	void setupCountry();

	const FieldConfig _config;
	const base::unique_qptr<RpWidget> _wrap;
	InputField *_input = nullptr;
	MaskedInputField *_masked = nullptr;
	QString _countryIso2;

};

} // namespace Payments::Ui
