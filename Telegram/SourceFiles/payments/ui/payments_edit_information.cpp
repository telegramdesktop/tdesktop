/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/ui/payments_edit_information.h"

#include "payments/ui/payments_panel_delegate.h"
#include "passport/ui/passport_details_row.h"
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

void EditInformation::setFocus(InformationField field) {
	_focusField = field;
	if (const auto control = controlForField(field)) {
		_scroll->ensureWidgetVisible(control);
		control->setFocusFast();
	}
}

void EditInformation::showError(InformationField field) {
	if (const auto control = controlForField(field)) {
		_scroll->ensureWidgetVisible(control);
		control->showError(QString());
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
	using Type = Passport::Ui::PanelDetailsType;
	auto maxLabelWidth = 0;
	if (_invoice.isShippingAddressRequested) {
		accumulate_max(
			maxLabelWidth,
			Row::LabelWidth(tr::lng_passport_street(tr::now)));
		accumulate_max(
			maxLabelWidth,
			Row::LabelWidth(tr::lng_passport_city(tr::now)));
		accumulate_max(
			maxLabelWidth,
			Row::LabelWidth(tr::lng_passport_state(tr::now)));
		accumulate_max(
			maxLabelWidth,
			Row::LabelWidth(tr::lng_passport_country(tr::now)));
		accumulate_max(
			maxLabelWidth,
			Row::LabelWidth(tr::lng_passport_postcode(tr::now)));
	}
	if (_invoice.isNameRequested) {
		accumulate_max(
			maxLabelWidth,
			Row::LabelWidth(tr::lng_payments_info_name(tr::now)));
	}
	if (_invoice.isEmailRequested) {
		accumulate_max(
			maxLabelWidth,
			Row::LabelWidth(tr::lng_payments_info_email(tr::now)));
	}
	if (_invoice.isPhoneRequested) {
		accumulate_max(
			maxLabelWidth,
			Row::LabelWidth(tr::lng_payments_info_phone(tr::now)));
	}
	if (_invoice.isShippingAddressRequested) {
		_street1 = inner->add(
			Row::Create(
				inner,
				showBox,
				QString(),
				Type::Text,
				tr::lng_passport_street(tr::now),
				maxLabelWidth,
				_information.shippingAddress.address1,
				QString(),
				kMaxStreetSize));
		_street2 = inner->add(
			Row::Create(
				inner,
				showBox,
				QString(),
				Type::Text,
				tr::lng_passport_street(tr::now),
				maxLabelWidth,
				_information.shippingAddress.address2,
				QString(),
				kMaxStreetSize));
		_city = inner->add(
			Row::Create(
				inner,
				showBox,
				QString(),
				Type::Text,
				tr::lng_passport_city(tr::now),
				maxLabelWidth,
				_information.shippingAddress.city,
				QString(),
				kMaxStreetSize));
		_state = inner->add(
			Row::Create(
				inner,
				showBox,
				QString(),
				Type::Text,
				tr::lng_passport_state(tr::now),
				maxLabelWidth,
				_information.shippingAddress.state,
				QString(),
				kMaxStreetSize));
		_country = inner->add(
			Row::Create(
				inner,
				showBox,
				QString(),
				Type::Country,
				tr::lng_passport_country(tr::now),
				maxLabelWidth,
				_information.shippingAddress.countryIso2,
				QString()));
		_postcode = inner->add(
			Row::Create(
				inner,
				showBox,
				QString(),
				Type::Postcode,
				tr::lng_passport_postcode(tr::now),
				maxLabelWidth,
				_information.shippingAddress.postcode,
				QString(),
				kMaxPostcodeSize));
		//StreetValidate, // #TODO payments
		//CityValidate,
		//CountryValidate,
		//CountryFormat,
		//PostcodeValidate,
	}
	if (_invoice.isNameRequested) {
		_name = inner->add(
			Row::Create(
				inner,
				showBox,
				QString(),
				Type::Text,
				tr::lng_payments_info_name(tr::now),
				maxLabelWidth,
				_information.name,
				QString(),
				kMaxNameSize));
	}
	if (_invoice.isEmailRequested) {
		_email = inner->add(
			Row::Create(
				inner,
				showBox,
				QString(),
				Type::Text,
				tr::lng_payments_info_email(tr::now),
				maxLabelWidth,
				_information.email,
				QString(),
				kMaxEmailSize));
	}
	if (_invoice.isPhoneRequested) {
		_phone = inner->add(
			Row::Create(
				inner,
				showBox,
				QString(),
				Type::Text,
				tr::lng_payments_info_phone(tr::now),
				maxLabelWidth,
				_information.phone,
				QString(),
				kMaxPhoneSize));
	}
	return inner;
}

void EditInformation::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void EditInformation::focusInEvent(QFocusEvent *e) {
	if (const auto control = controlForField(_focusField)) {
		control->setFocusFast();
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

auto EditInformation::controlForField(InformationField field) const -> Row* {
	switch (field) {
	case InformationField::ShippingStreet: return _street1;
	case InformationField::ShippingCity: return _city;
	case InformationField::ShippingState: return _state;
	case InformationField::ShippingCountry: return _country;
	case InformationField::ShippingPostcode: return _postcode;
	case InformationField::Name: return _name;
	case InformationField::Email: return _email;
	case InformationField::Phone: return _phone;
	}
	Unexpected("Unknown field in EditInformation::controlForField.");
}

RequestedInformation EditInformation::collect() const {
	return {
		.name = _name ? _name->valueCurrent() : QString(),
		.phone = _phone ? _phone->valueCurrent() : QString(),
		.email = _email ? _email->valueCurrent() : QString(),
		.shippingAddress = {
			.address1 = _street1 ? _street1->valueCurrent() : QString(),
			.address2 = _street2 ? _street2->valueCurrent() : QString(),
			.city = _city ? _city->valueCurrent() : QString(),
			.state = _state ? _state->valueCurrent() : QString(),
			.countryIso2 = _country ? _country->valueCurrent() : QString(),
			.postcode = _postcode ? _postcode->valueCurrent() : QString(),
		},
	};
}

} // namespace Payments::Ui
