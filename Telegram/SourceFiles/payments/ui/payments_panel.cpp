/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/ui/payments_panel.h"

#include "payments/ui/payments_form_summary.h"
#include "payments/ui/payments_edit_information.h"
#include "payments/ui/payments_edit_card.h"
#include "payments/ui/payments_panel_delegate.h"
#include "ui/widgets/separate_panel.h"
#include "ui/boxes/single_choice_box.h"
#include "lang/lang_keys.h"
#include "styles/style_payments.h"
#include "styles/style_passport.h"

namespace Payments::Ui {

Panel::Panel(not_null<PanelDelegate*> delegate)
: _delegate(delegate)
, _widget(std::make_unique<SeparatePanel>()) {
	_widget->setTitle(tr::lng_payments_checkout_title());
	_widget->setInnerSize(st::passportPanelSize);
	_widget->setWindowFlag(Qt::WindowStaysOnTopHint, false);

	_widget->closeRequests(
	) | rpl::start_with_next([=] {
		_delegate->panelRequestClose();
	}, _widget->lifetime());

	_widget->closeEvents(
	) | rpl::start_with_next([=] {
		_delegate->panelCloseSure();
	}, _widget->lifetime());
}

Panel::~Panel() = default;

void Panel::requestActivate() {
	_widget->showAndActivate();
}

void Panel::showForm(
		const Invoice &invoice,
		const RequestedInformation &current,
		const NativePaymentDetails &native,
		const ShippingOptions &options) {
	_widget->showInner(
		base::make_unique_q<FormSummary>(
			_widget.get(),
			invoice,
			current,
			native,
			options,
			_delegate));
	_widget->setBackAllowed(false);
}

void Panel::showEditInformation(
		const Invoice &invoice,
		const RequestedInformation &current,
		InformationField field) {
	auto edit = base::make_unique_q<EditInformation>(
		_widget.get(),
		invoice,
		current,
		field,
		_delegate);
	_weakEditInformation = edit.get();
	_widget->showInner(std::move(edit));
	_widget->setBackAllowed(true);
	_weakEditInformation->setFocus(field);
}

void Panel::showInformationError(
		const Invoice &invoice,
		const RequestedInformation &current,
		InformationField field) {
	if (_weakEditInformation) {
		_weakEditInformation->showError(field);
	} else {
		showEditInformation(invoice, current, field);
		if (_weakEditInformation
			&& field == InformationField::ShippingCountry) {
			_weakEditInformation->showError(field);
		}
	}
}

void Panel::chooseShippingOption(const ShippingOptions &options) {
	showBox(Box([=](not_null<Ui::GenericBox*> box) {
		auto list = options.list | ranges::views::transform(
			&ShippingOption::title
		) | ranges::to_vector;
		const auto i = ranges::find(
			options.list,
			options.selectedId,
			&ShippingOption::id);
		const auto save = [=](int option) {
			_delegate->panelChangeShippingOption(options.list[option].id);
		};
		SingleChoiceBox(box, {
			.title = tr::lng_payments_shipping_method(),
			.options = list,
			.initialSelection = (i != end(options.list)
				? (i - begin(options.list))
				: -1),
			.callback = save,
		});
	}));
}

void Panel::choosePaymentMethod(const NativePaymentDetails &native) {
	Expects(native.supported);

	if (!native.ready) {
		showEditCard(native, CardField::Number);
		return;
	}
	const auto title = native.credentialsTitle;
	showBox(Box([=](not_null<Ui::GenericBox*> box) {
		const auto save = [=](int option) {
			if (option) {
				showEditCard(native, CardField::Number);
			}
		};
		SingleChoiceBox(box, {
			.title = tr::lng_payments_payment_method(),
			.options = { native.credentialsTitle, "New Card..." }, // #TODO payments lang
			.initialSelection = 0,
			.callback = save,
		});
	}));
}

void Panel::showEditCard(
		const NativePaymentDetails &native,
		CardField field) {
	auto edit = base::make_unique_q<EditCard>(
		_widget.get(),
		native,
		field,
		_delegate);
	_weakEditCard = edit.get();
	_widget->showInner(std::move(edit));
	_widget->setBackAllowed(true);
	_weakEditCard->setFocus(field);
}

void Panel::showCardError(
		const NativePaymentDetails &native,
		CardField field) {
	if (_weakEditCard) {
		_weakEditCard->showError(field);
	} else {
		showEditCard(native, field);
		if (_weakEditCard
			&& field == CardField::AddressCountry) {
			_weakEditCard->showError(field);
		}
	}
}

rpl::producer<> Panel::backRequests() const {
	return _widget->backRequests();
}

void Panel::showBox(object_ptr<Ui::BoxContent> box) {
	_widget->showBox(
		std::move(box),
		Ui::LayerOption::KeepOther,
		anim::type::normal);
}

void Panel::showToast(const TextWithEntities &text) {
	_widget->showToast(text);
}

rpl::lifetime &Panel::lifetime() {
	return _widget->lifetime();
}

} // namespace Payments::Ui
