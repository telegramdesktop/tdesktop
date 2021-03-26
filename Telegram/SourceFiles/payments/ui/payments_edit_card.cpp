/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/ui/payments_edit_card.h"

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
	if (const auto control = lookupField(field)) {
		_scroll->ensureWidgetVisible(control->widget());
		control->setFocus();
	}
}

void EditCard::setFocusFast(CardField field) {
	_focusField = field;
	if (const auto control = lookupField(field)) {
		_scroll->ensureWidgetVisible(control->widget());
		control->setFocusFast();
	}
}

void EditCard::showError(CardField field) {
	if (const auto control = lookupField(field)) {
		_scroll->ensureWidgetVisible(control->widget());
		control->showError();
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
	const auto add = [&](FieldConfig &&config) {
		auto result = std::make_unique<Field>(inner, std::move(config));
		inner->add(result->ownedWidget(), st::paymentsFieldPadding);
		return result;
	};
	_number = add({
		.type = FieldType::CardNumber,
		.placeholder = tr::lng_payments_card_number(),
		.required = true,
	});
	if (_native.needCardholderName) {
		_name = add({
			.type = FieldType::CardNumber,
			.placeholder = tr::lng_payments_card_holder(),
			.required = true,
		});
	}
	auto container = inner->add(
		object_ptr<FixedHeightWidget>(
			inner,
			_number->widget()->height()),
		st::paymentsFieldPadding);
	_expire = std::make_unique<Field>(container, FieldConfig{
		.type = FieldType::CardExpireDate,
		.placeholder = rpl::single(u"MM / YY"_q),
		.required = true,
	});
	_cvc = std::make_unique<Field>(container, FieldConfig{
		.type = FieldType::CardCVC,
		.placeholder = rpl::single(u"CVC"_q),
		.required = true,
	});
	container->widthValue(
	) | rpl::start_with_next([=](int width) {
		const auto left = (width - st::paymentsExpireCvcSkip) / 2;
		const auto right = width - st::paymentsExpireCvcSkip - left;
		_expire->widget()->resizeToWidth(left);
		_cvc->widget()->resizeToWidth(right);
		_expire->widget()->moveToLeft(0, 0, width);
		_cvc->widget()->moveToRight(0, 0, width);
	}, container->lifetime());
	if (_native.needCountry || _native.needZip) {
		inner->add(
			object_ptr<Ui::FlatLabel>(
				inner,
				tr::lng_payments_billing_address(),
				st::paymentsBillingInformationTitle),
			st::paymentsBillingInformationTitlePadding);
	}
	if (_native.needCountry) {
		_country = add({
			.type = FieldType::Country,
			.placeholder = tr::lng_payments_billing_country(),
			.showBox = showBox,
			.defaultCountry = _native.defaultCountry,
			.required = true,
		});
	}
	if (_native.needZip) {
		_zip = add({
			.type = FieldType::Text,
			.placeholder = tr::lng_payments_billing_zip_code(),
			.maxLength = kMaxPostcodeSize,
			.required = true,
		});
	}
	return inner;
}

void EditCard::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void EditCard::focusInEvent(QFocusEvent *e) {
	if (const auto control = lookupField(_focusField)) {
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

auto EditCard::lookupField(CardField field) const -> Field* {
	switch (field) {
	case CardField::Number: return _number.get();
	case CardField::CVC: return _cvc.get();
	case CardField::ExpireDate: return _expire.get();
	case CardField::Name: return _name.get();
	case CardField::AddressCountry: return _country.get();
	case CardField::AddressZip: return _zip.get();
	}
	Unexpected("Unknown field in EditCard::controlForField.");
}

UncheckedCardDetails EditCard::collect() const {
	return {
		.number = _number ? _number->value() : QString(),
		.cvc = _cvc ? _cvc->value() : QString(),
		.expireYear = _expire ? ExtractYear(_expire->value()) : 0,
		.expireMonth = _expire ? ExtractMonth(_expire->value()) : 0,
		.cardholderName = _name ? _name->value() : QString(),
		.addressCountry = _country ? _country->value() : QString(),
		.addressZip = _zip ? _zip->value() : QString(),
	};
}

} // namespace Payments::Ui
