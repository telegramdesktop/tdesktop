/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/ui/payments_panel.h"

#include "payments/ui/payments_form_summary.h"
#include "payments/ui/payments_panel_delegate.h"
#include "ui/widgets/separate_panel.h"
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

void Panel::showForm(const Invoice &invoice) {
	_widget->showInner(
		base::make_unique_q<FormSummary>(_widget.get(), invoice, _delegate));
}

} // namespace Payments::Ui
