/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/ui/payments_edit_card.h"

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

constexpr auto kMaxPostcodeSize = 10;

[[nodiscard]] uint32 ExtractYear(const QString &value) {
	return value.split('/').value(1).toInt() + 2000;
}

[[nodiscard]] uint32 ExtractMonth(const QString &value) {
	return value.split('/').value(0).toInt();
}

} // namespace

EditCard::EditCard(
	QWidget *parent,
	const NativeMethodDetails &native,
	CardField field,
	not_null<PanelDelegate*> delegate)
: _delegate(delegate)
, _native(native)
, _scroll(this, st::passportPanelScroll)
, _topShadow(this)
, _bottomShadow(this)
, _done(
		this,
		tr::lng_about_done(),
		st::passportPanelSaveValue) {
	setupControls();
}

void EditCard::setFocus(CardField field) {
	_focusField = field;
	if (const auto control = controlForField(field)) {
		_scroll->ensureWidgetVisible(control);
		control->setFocusFast();
	}
}

void EditCard::showError(CardField field) {
	if (const auto control = controlForField(field)) {
		_scroll->ensureWidgetVisible(control);
		control->showError(QString());
	}
}

void EditCard::setupControls() {
	const auto inner = setupContent();

	_done->addClickHandler([=] {
		_delegate->panelValidateCard(collect());
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

not_null<RpWidget*> EditCard::setupContent() {
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
	accumulate_max(
		maxLabelWidth,
		Row::LabelWidth("Card Number"));
	accumulate_max(
		maxLabelWidth,
		Row::LabelWidth("CVC"));
	accumulate_max(
		maxLabelWidth,
		Row::LabelWidth("MM/YY"));
	if (_native.needCardholderName) {
		accumulate_max(
			maxLabelWidth,
			Row::LabelWidth("Cardholder Name"));
	}
	if (_native.needCountry) {
		accumulate_max(
			maxLabelWidth,
			Row::LabelWidth("Billing Country"));
	}
	if (_native.needZip) {
		accumulate_max(
			maxLabelWidth,
			Row::LabelWidth("Billing Zip"));
	}
	_number = inner->add(
		Row::Create(
			inner,
			showBox,
			QString(),
			Type::Text,
			"Card Number",
			maxLabelWidth,
			QString(),
			QString(),
			1024));
	_cvc = inner->add(
		Row::Create(
			inner,
			showBox,
			QString(),
			Type::Text,
			"CVC",
			maxLabelWidth,
			QString(),
			QString(),
			1024));
	_expire = inner->add(
		Row::Create(
			inner,
			showBox,
			QString(),
			Type::Text,
			"MM/YY",
			maxLabelWidth,
			QString(),
			QString(),
			1024));
	if (_native.needCardholderName) {
		_name = inner->add(
			Row::Create(
				inner,
				showBox,
				QString(),
				Type::Text,
				"Cardholder Name",
				maxLabelWidth,
				QString(),
				QString(),
				1024));
	}
	if (_native.needCountry) {
		_country = inner->add(
			Row::Create(
				inner,
				showBox,
				QString(),
				Type::Country,
				"Billing Country",
				maxLabelWidth,
				QString(),
				QString()));
	}
	if (_native.needZip) {
		_zip = inner->add(
			Row::Create(
				inner,
				showBox,
				QString(),
				Type::Postcode,
				"Billing Zip Code",
				maxLabelWidth,
				QString(),
				QString(),
				kMaxPostcodeSize));
	}
	return inner;
}

void EditCard::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void EditCard::focusInEvent(QFocusEvent *e) {
	if (const auto control = controlForField(_focusField)) {
		control->setFocusFast();
	}
}

void EditCard::updateControlsGeometry() {
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

auto EditCard::controlForField(CardField field) const -> Row* {
	switch (field) {
	case CardField::Number: return _number;
	case CardField::CVC: return _cvc;
	case CardField::ExpireDate: return _expire;
	case CardField::Name: return _name;
	case CardField::AddressCountry: return _country;
	case CardField::AddressZip: return _zip;
	}
	Unexpected("Unknown field in EditCard::controlForField.");
}

UncheckedCardDetails EditCard::collect() const {
	return {
		.number = _number ? _number->valueCurrent() : QString(),
		.cvc = _cvc ? _cvc->valueCurrent() : QString(),
		.expireYear = _expire ? ExtractYear(_expire->valueCurrent()) : 0,
		.expireMonth = _expire ? ExtractMonth(_expire->valueCurrent()) : 0,
		.cardholderName = _name ? _name->valueCurrent() : QString(),
		.addressCountry = _country ? _country->valueCurrent() : QString(),
		.addressZip = _zip ? _zip->valueCurrent() : QString(),
	};
}

} // namespace Payments::Ui
