/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/ui/payments_field.h"

#include "ui/widgets/input_fields.h"
#include "ui/boxes/country_select_box.h"
#include "data/data_countries.h"
#include "base/platform/base_platform_info.h"
#include "styles/style_payments.h"

namespace Payments::Ui {
namespace {

[[nodiscard]] QString Parse(const FieldConfig &config) {
	if (config.type == FieldType::Country) {
		return Data::CountryNameByISO2(config.value);
	}
	return config.value;
}

[[nodiscard]] QString Format(
		const FieldConfig &config,
		const QString &parsed,
		const QString &countryIso2) {
	if (config.type == FieldType::Country) {
		return countryIso2;
	}
	return parsed;
}

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
			Parse(config));
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
			Parse(config));
	}
	Unexpected("FieldType in Payments::Ui::LookupMaskedField.");
}

} // namespace

Field::Field(QWidget *parent, FieldConfig &&config)
: _config(config)
, _wrap(CreateWrap(parent, config))
, _input(LookupInputField(_wrap.get(), config))
, _masked(LookupMaskedField(_wrap.get(), config))
, _countryIso2(config.value) {
	if (_masked) {
		setupMaskedGeometry();
	}
	if (_config.type == FieldType::Country) {
		setupCountry();
	}
}

RpWidget *Field::widget() const {
	return _wrap.get();
}

object_ptr<RpWidget> Field::ownedWidget() const {
	return object_ptr<RpWidget>::fromRaw(_wrap.get());
}

QString Field::value() const {
	return Format(
		_config,
		_input ? _input->getLastText() : _masked->getLastText(),
		_countryIso2);
}

void Field::setupMaskedGeometry() {
	Expects(_masked != nullptr);

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

void Field::setupCountry() {
	Expects(_config.type == FieldType::Country);
	Expects(_masked != nullptr);

	QObject::connect(_masked, &MaskedInputField::focused, [=] {
		setFocus();

		const auto name = Data::CountryNameByISO2(_countryIso2);
		const auto country = !name.isEmpty()
			? _countryIso2
			: !_config.defaultCountry.isEmpty()
			? _config.defaultCountry
			: Platform::SystemCountry();
		auto box = Box<CountrySelectBox>(
			country,
			CountrySelectBox::Type::Countries);
		const auto raw = box.data();
		raw->countryChosen(
		) | rpl::start_with_next([=](QString iso2) {
			_countryIso2 = iso2;
			_masked->setText(Data::CountryNameByISO2(iso2));
			_masked->hideError();
			setFocus();
			raw->closeBox();
		}, _masked->lifetime());
		_config.showBox(std::move(box));
	});
}

void Field::setFocus() {
	if (_config.type == FieldType::Country) {
		_wrap->setFocus();
	} else if (_input) {
		_input->setFocus();
	} else {
		_masked->setFocus();
	}
}

void Field::setFocusFast() {
	if (_config.type == FieldType::Country) {
		setFocus();
	} else if (_input) {
		_input->setFocusFast();
	} else {
		_masked->setFocusFast();
	}
}

void Field::showError() {
	if (_config.type == FieldType::Country) {
		setFocus();
		_masked->showErrorNoFocus();
	} else if (_input) {
		_input->showError();
	} else {
		_masked->showError();
	}
}

} // namespace Payments::Ui
