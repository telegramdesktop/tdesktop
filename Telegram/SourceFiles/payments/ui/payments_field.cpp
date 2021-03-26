/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/ui/payments_field.h"

#include "ui/widgets/input_fields.h"
#include "styles/style_payments.h"

namespace Payments::Ui {
namespace {

[[nodiscard]] bool UseMaskedField(FieldType type) {
	switch (type) {
	case FieldType::Text:
	case FieldType::Email:
		return false;
	case FieldType::CardNumber:
	case FieldType::CardExpireDate:
	case FieldType::CardCVC:
	case FieldType::Country:
	case FieldType::Phone:
		return true;
	}
	Unexpected("FieldType in Payments::Ui::UseMaskedField.");
}

[[nodiscard]] base::unique_qptr<RpWidget> CreateWrap(
		QWidget *parent,
		FieldConfig &config) {
	switch (config.type) {
	case FieldType::Text:
	case FieldType::Email:
		return base::make_unique_q<InputField>(
			parent,
			st::paymentsField,
			std::move(config.placeholder),
			config.value);
	case FieldType::CardNumber:
	case FieldType::CardExpireDate:
	case FieldType::CardCVC:
	case FieldType::Country:
	case FieldType::Phone:
		return base::make_unique_q<RpWidget>(parent);
	}
	Unexpected("FieldType in Payments::Ui::CreateWrap.");
}

[[nodiscard]] InputField *LookupInputField(
		not_null<RpWidget*> wrap,
		FieldConfig &config) {
	return UseMaskedField(config.type)
		? nullptr
		: static_cast<InputField*>(wrap.get());
}

[[nodiscard]] MaskedInputField *LookupMaskedField(
		not_null<RpWidget*> wrap,
		FieldConfig &config) {
	if (!UseMaskedField(config.type)) {
		return nullptr;
	}
	switch (config.type) {
	case FieldType::Text:
	case FieldType::Email:
		return nullptr;
	case FieldType::CardNumber:
	case FieldType::CardExpireDate:
	case FieldType::CardCVC:
	case FieldType::Country:
	case FieldType::Phone:
		return CreateChild<MaskedInputField>(
			wrap.get(),
			st::paymentsField,
			std::move(config.placeholder),
			config.value);
	}
	Unexpected("FieldType in Payments::Ui::LookupMaskedField.");
}

} // namespace

Field::Field(QWidget *parent, FieldConfig &&config)
: _type(config.type)
, _wrap(CreateWrap(parent, config))
, _input(LookupInputField(_wrap.get(), config))
, _masked(LookupMaskedField(_wrap.get(), config)) {
	if (_masked) {
		_wrap->resize(_masked->size());
		_wrap->widthValue(
		) | rpl::start_with_next([=](int width) {
			_masked->resize(width, _masked->height());
		}, _masked->lifetime());
		_masked->heightValue(
		) | rpl::start_with_next([=](int height) {
			_wrap->resize(_wrap->width(), height);
		}, _masked->lifetime());
	}
}

RpWidget *Field::widget() const {
	return _wrap.get();
}

object_ptr<RpWidget> Field::ownedWidget() const {
	return object_ptr<RpWidget>::fromRaw(_wrap.get());
}

[[nodiscard]] QString Field::value() const {
	return _input ? _input->getLastText() : _masked->getLastText();
}

void Field::setFocus() {
	if (_input) {
		_input->setFocus();
	} else {
		_masked->setFocus();
	}
}

void Field::setFocusFast() {
	if (_input) {
		_input->setFocusFast();
	} else {
		_masked->setFocusFast();
	}
}

void Field::showError() {
	if (_input) {
		_input->showError();
	} else {
		_masked->showError();
	}
}

} // namespace Payments::Ui
