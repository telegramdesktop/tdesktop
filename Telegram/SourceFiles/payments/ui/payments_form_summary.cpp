/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/ui/payments_form_summary.h"

#include "payments/ui/payments_panel_delegate.h"
#include "passport/ui/passport_form_row.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/text/format_values.h"
#include "lang/lang_keys.h"
#include "styles/style_payments.h"
#include "styles/style_passport.h"

namespace Payments::Ui {

using namespace ::Ui;
using namespace Passport::Ui;

class PanelDelegate;

FormSummary::FormSummary(
	QWidget *parent,
	const Invoice &invoice,
	const RequestedInformation &current,
	const NativePaymentDetails &native,
	const ShippingOptions &options,
	not_null<PanelDelegate*> delegate)
: _delegate(delegate)
, _invoice(invoice)
, _native(native)
, _options(options)
, _information(current)
, _scroll(this, st::passportPanelScroll)
, _topShadow(this)
, _bottomShadow(this)
, _submit(
		this,
		tr::lng_payments_pay_amount(
			lt_amount,
			rpl::single(computeTotalAmount())),
		st::paymentsPanelSubmit) {
	setupControls();
}

QString FormSummary::computeAmount(int64 amount) const {
	return FillAmountAndCurrency(amount, _invoice.currency);
}

QString FormSummary::computeTotalAmount() const {
	const auto total = ranges::accumulate(
		_invoice.prices,
		int64(0),
		std::plus<>(),
		&LabeledPrice::price);
	const auto selected = ranges::find(
		_options.list,
		_options.selectedId,
		&ShippingOption::id);
	const auto shipping = (selected != end(_options.list))
		? ranges::accumulate(
			selected->prices,
			int64(0),
			std::plus<>(),
			&LabeledPrice::price)
		: int64(0);
	return computeAmount(total + shipping);
}

void FormSummary::setupControls() {
	const auto inner = setupContent();

	_submit->addClickHandler([=] {
		_delegate->panelSubmit();
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

not_null<Ui::RpWidget*> FormSummary::setupContent() {
	const auto inner = _scroll->setOwnedWidget(
		object_ptr<Ui::VerticalLayout>(this));

	_scroll->widthValue(
	) | rpl::start_with_next([=](int width) {
		inner->resizeToWidth(width);
	}, inner->lifetime());

	for (const auto &price : _invoice.prices) {
		inner->add(
			object_ptr<Ui::FlatLabel>(
				inner,
				price.label + ": " + computeAmount(price.price),
				st::passportFormPolicy),
			st::paymentsFormPricePadding);
	}
	const auto selected = ranges::find(
		_options.list,
		_options.selectedId,
		&ShippingOption::id);
	if (selected != end(_options.list)) {
		for (const auto &price : selected->prices) {
			inner->add(
				object_ptr<Ui::FlatLabel>(
					inner,
					price.label + ": " + computeAmount(price.price),
					st::passportFormPolicy),
				st::paymentsFormPricePadding);
		}
	}
	inner->add(
		object_ptr<Ui::FlatLabel>(
			inner,
			"Total: " + computeTotalAmount(),
			st::passportFormHeader),
		st::passportFormHeaderPadding);

	inner->add(
		object_ptr<Ui::BoxContentDivider>(
			inner,
			st::passportFormDividerHeight),
		{ 0, 0, 0, st::passportFormHeaderPadding.top() });

	if (_native.supported) {
		const auto method = inner->add(object_ptr<FormRow>(inner));
		method->addClickHandler([=] {
			_delegate->panelEditPaymentMethod();
		});
		method->updateContent(
			tr::lng_payments_payment_method(tr::now),
			(_native.ready
				? _native.credentialsTitle
				: tr::lng_payments_payment_method_ph(tr::now)),
			_native.ready,
			false,
			anim::type::instant);
	}
	if (_invoice.isShippingAddressRequested) {
		const auto info = inner->add(object_ptr<FormRow>(inner));
		info->addClickHandler([=] {
			_delegate->panelEditShippingInformation();
		});
		auto list = QStringList();
		const auto push = [&](const QString &value) {
			if (!value.isEmpty()) {
				list.push_back(value);
			}
		};
		push(_information.shippingAddress.address1);
		push(_information.shippingAddress.address2);
		push(_information.shippingAddress.city);
		push(_information.shippingAddress.state);
		push(_information.shippingAddress.countryIso2);
		push(_information.shippingAddress.postcode);
		info->updateContent(
			tr::lng_payments_shipping_address(tr::now),
			(list.isEmpty()
				? tr::lng_payments_shipping_address_ph(tr::now)
				: list.join(", ")),
			!list.isEmpty(),
			false,
			anim::type::instant);
	}
	if (!_options.list.empty()) {
		const auto options = inner->add(object_ptr<FormRow>(inner));
		options->addClickHandler([=] {
			_delegate->panelChooseShippingOption();
		});
		options->updateContent(
			tr::lng_payments_shipping_method(tr::now),
			(selected != end(_options.list)
				? selected->title
				: tr::lng_payments_shipping_method_ph(tr::now)),
			(selected != end(_options.list)),
			false,
			anim::type::instant);
	}
	if (_invoice.isNameRequested) {
		const auto name = inner->add(object_ptr<FormRow>(inner));
		name->addClickHandler([=] { _delegate->panelEditName(); });
		name->updateContent(
			tr::lng_payments_info_name(tr::now),
			(_information.name.isEmpty()
				? tr::lng_payments_info_name_ph(tr::now)
				: _information.name),
			!_information.name.isEmpty(),
			false,
			anim::type::instant);
	}
	if (_invoice.isEmailRequested) {
		const auto email = inner->add(object_ptr<FormRow>(inner));
		email->addClickHandler([=] { _delegate->panelEditEmail(); });
		email->updateContent(
			tr::lng_payments_info_email(tr::now),
			(_information.email.isEmpty()
				? tr::lng_payments_info_email_ph(tr::now)
				: _information.email),
			!_information.email.isEmpty(),
			false,
			anim::type::instant);
	}
	if (_invoice.isPhoneRequested) {
		const auto phone = inner->add(object_ptr<FormRow>(inner));
		phone->addClickHandler([=] { _delegate->panelEditPhone(); });
		phone->updateContent(
			tr::lng_payments_info_phone(tr::now),
			(_information.phone.isEmpty()
				? tr::lng_payments_info_phone_ph(tr::now)
				: _information.phone),
			!_information.phone.isEmpty(),
			false,
			anim::type::instant);
	}

	return inner;
}

void FormSummary::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void FormSummary::updateControlsGeometry() {
	const auto submitTop = height() - _submit->height();
	_scroll->setGeometry(0, 0, width(), submitTop);
	_topShadow->resizeToWidth(width());
	_topShadow->moveToLeft(0, 0);
	_bottomShadow->resizeToWidth(width());
	_bottomShadow->moveToLeft(0, submitTop - st::lineWidth);
	_submit->setFullWidth(width());
	_submit->moveToLeft(0, submitTop);

	_scroll->updateBars();
}

} // namespace Payments::Ui
