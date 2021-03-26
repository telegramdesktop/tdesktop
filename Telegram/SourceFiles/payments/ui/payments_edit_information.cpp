/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/ui/payments_edit_information.h"

#include "payments/ui/payments_panel_delegate.h"
#include "payments/ui/payments_field.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/fade_wrap.h"
#include "lang/lang_keys.h"
#include "styles/style_payments.h"
#include "styles/style_passport.h"

namespace Payments::Ui {
namespace {

constexpr auto kMaxStreetSize = 64;
constexpr auto kMaxPostcodeSize = 10;
constexpr auto kMaxNameSize = 64;
constexpr auto kMaxEmailSize = 128;
constexpr auto kMaxPhoneSize = 16;

} // namespace

EditInformation::EditInformation(
	QWidget *parent,
	const Invoice &invoice,
	const RequestedInformation &current,
	InformationField field,
	not_null<PanelDelegate*> delegate)
: _delegate(delegate)
, _invoice(invoice)
, _information(current)
, _scroll(this, st::passportPanelScroll)
, _topShadow(this)
, _bottomShadow(this)
, _done(
		this,
		tr::lng_about_done(),
		st::passportPanelSaveValue) {
	setupControls();
}

EditInformation::~EditInformation() = default;

void EditInformation::setFocus(InformationField field) {
	_focusField = field;
	if (const auto control = lookupField(field)) {
		_scroll->ensureWidgetVisible(control->widget());
		control->setFocus();
	}
}

void EditInformation::setFocusFast(InformationField field) {
	_focusField = field;
	if (const auto control = lookupField(field)) {
		_scroll->ensureWidgetVisible(control->widget());
		control->setFocusFast();
	}
}

void EditInformation::showError(InformationField field) {
	if (const auto control = lookupField(field)) {
		_scroll->ensureWidgetVisible(control->widget());
		control->showError();
	}
}

void EditInformation::setupControls() {
	const auto inner = setupContent();

	_done->addClickHandler([=] {
		_delegate->panelValidateInformation(collect());
	});

	using namespace rpl::mappers;

	_topShadow->toggleOn(
		_scroll->scrollTopValue() | rpl::map(_1 > 0));
	_bottomShadow->toggleOn(rpl::combine(
		_scroll->scrollTopValue(),
		_scroll->heightValue(),
		inner->heightValue(),
		_1 + _2 < _3));
}

not_null<RpWidget*> EditInformation::setupContent() {
	const auto inner = _scroll->setOwnedWidget(
		object_ptr<VerticalLayout>(this));

	_scroll->widthValue(
	) | rpl::start_with_next([=](int width) {
		inner->resizeToWidth(width);
	}, inner->lifetime());

	const auto showBox = [=](object_ptr<BoxContent> box) {
		_delegate->panelShowBox(std::move(box));
	};
	const auto add = [&](FieldConfig &&config) {
		auto result = std::make_unique<Field>(inner, std::move(config));
		inner->add(result->ownedWidget(), st::paymentsFieldPadding);
		return result;
	};
	if (_invoice.isShippingAddressRequested) {
		_street1 = add({
			.placeholder = tr::lng_payments_address_street1(),
			.value = _information.shippingAddress.address1,
			.maxLength = kMaxStreetSize,
			.required = true,
		});
		_street2 = add({
			.placeholder = tr::lng_payments_address_street2(),
			.value = _information.shippingAddress.address2,
			.maxLength = kMaxStreetSize,
		});
		_city = add({
			.placeholder = tr::lng_payments_address_city(),
			.value = _information.shippingAddress.city,
			.required = true,
		});
		_state = add({
			.placeholder = tr::lng_payments_address_state(),
			.value = _information.shippingAddress.state,
		});
		_country = add({
			.type = FieldType::Country,
			.placeholder = tr::lng_payments_address_country(),
			.value = _information.shippingAddress.countryIso2,
			.required = true,
		});
		_postcode = add({
			.placeholder = tr::lng_payments_address_postcode(),
			.value = _information.shippingAddress.postcode,
			.maxLength = kMaxPostcodeSize,
			.required = true,
		});
		//StreetValidate, // #TODO payments
		//CityValidate,
		//CountryValidate,
		//CountryFormat,
		//PostcodeValidate,
	}
	if (_invoice.isNameRequested) {
		_name = add({
			.placeholder = tr::lng_payments_info_name(),
			.value = _information.name,
			.maxLength = kMaxNameSize,
			.required = true,
		});
	}
	if (_invoice.isEmailRequested) {
		_email = add({
			.placeholder = tr::lng_payments_info_email(),
			.value = _information.email,
			.maxLength = kMaxEmailSize,
			.required = true,
		});
	}
	if (_invoice.isPhoneRequested) {
		_phone = add({
			.placeholder = tr::lng_payments_info_phone(),
			.value = _information.phone,
			.maxLength = kMaxPhoneSize,
			.required = true,
		});
	}
	return inner;
}

void EditInformation::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void EditInformation::focusInEvent(QFocusEvent *e) {
	if (const auto control = lookupField(_focusField)) {
		control->setFocus();
	}
}

void EditInformation::updateControlsGeometry() {
	const auto submitTop = height() - _done->height();
	_scroll->setGeometry(0, 0, width(), submitTop);
	_topShadow->resizeToWidth(width());
	_topShadow->moveToLeft(0, 0);
	_bottomShadow->resizeToWidth(width());
	_bottomShadow->moveToLeft(0, submitTop - st::lineWidth);
	_done->setFullWidth(width());
	_done->moveToLeft(0, submitTop);

	_scroll->updateBars();
}

auto EditInformation::lookupField(InformationField field) const -> Field* {
	switch (field) {
	case InformationField::ShippingStreet: return _street1.get();
	case InformationField::ShippingCity: return _city.get();
	case InformationField::ShippingState: return _state.get();
	case InformationField::ShippingCountry: return _country.get();
	case InformationField::ShippingPostcode: return _postcode.get();
	case InformationField::Name: return _name.get();
	case InformationField::Email: return _email.get();
	case InformationField::Phone: return _phone.get();
	}
	Unexpected("Unknown field in EditInformation::lookupField.");
}

RequestedInformation EditInformation::collect() const {
	return {
		.name = _name ? _name->value() : QString(),
		.phone = _phone ? _phone->value() : QString(),
		.email = _email ? _email->value() : QString(),
		.shippingAddress = {
			.address1 = _street1 ? _street1->value() : QString(),
			.address2 = _street2 ? _street2->value() : QString(),
			.city = _city ? _city->value() : QString(),
			.state = _state ? _state->value() : QString(),
			.countryIso2 = _country ? _country->value() : QString(),
			.postcode = _postcode ? _postcode->value() : QString(),
		},
	};
}

} // namespace Payments::Ui
