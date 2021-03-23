/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/ui/payments_panel.h"

#include "payments/ui/payments_form_summary.h"
#include "payments/ui/payments_edit_information.h"
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
		const ShippingOptions &options) {
	_widget->showInner(
		base::make_unique_q<FormSummary>(
			_widget.get(),
			invoice,
			current,
			options,
			_delegate));
}

void Panel::showEditInformation(
		const Invoice &invoice,
		const RequestedInformation &current,
		EditField field) {
	_widget->showInner(base::make_unique_q<EditInformation>(
		_widget.get(),
		invoice,
		current,
		field,
		_delegate));
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

void Panel::showBox(object_ptr<Ui::BoxContent> box) {
	_widget->showBox(
		std::move(box),
		Ui::LayerOption::KeepOther,
		anim::type::normal);
}

void Panel::showToast(const QString &text) {
	_widget->showToast(text);
}

} // namespace Payments::Ui
