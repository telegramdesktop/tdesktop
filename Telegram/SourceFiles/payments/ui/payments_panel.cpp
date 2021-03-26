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
#include "webview/webview_embed.h"
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

Panel::~Panel() {
	// Destroy _widget before _webview.
	_widget = nullptr;
}

void Panel::requestActivate() {
	_widget->showAndActivate();
}

void Panel::showForm(
		const Invoice &invoice,
		const RequestedInformation &current,
		const PaymentMethodDetails &method,
		const ShippingOptions &options) {
	auto form = base::make_unique_q<FormSummary>(
		_widget.get(),
		invoice,
		current,
		method,
		options,
		_delegate);
	_weakFormSummary = form.get();
	_widget->showInner(std::move(form));
	_widget->setBackAllowed(false);
}

void Panel::updateFormThumbnail(const QImage &thumbnail) {
	if (_weakFormSummary) {
		_weakFormSummary->updateThumbnail(thumbnail);
	}
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

void Panel::showEditPaymentMethod(const PaymentMethodDetails &method) {
	if (method.native.supported) {
		showEditCard(method.native, CardField::Number);
	} else if (!showWebview(method.url, true)) {
		// #TODO payments errors not supported
	}
}

bool Panel::showWebview(const QString &url, bool allowBack) {
	if (!_webview && !createWebview()) {
		return false;
	}
	_webview->navigate(url);
	_widget->setBackAllowed(allowBack);
	return true;
}

bool Panel::createWebview() {
	auto container = base::make_unique_q<RpWidget>(_widget.get());

	container->setGeometry(_widget->innerGeometry());
	container->show();

	_webview = std::make_unique<Webview::Window>(
		container.get(),
		Webview::WindowConfig{
			.userDataPath = _delegate->panelWebviewDataPath(),
		});
	const auto raw = _webview.get();
	QObject::connect(container.get(), &QObject::destroyed, [=] {
		if (_webview.get() == raw) {
			_webview = nullptr;
		}
	});
	if (!raw->widget()) {
		return false;
	}

	container->geometryValue(
	) | rpl::start_with_next([=](QRect geometry) {
		raw->widget()->setGeometry(geometry);
	}, container->lifetime());

	raw->setMessageHandler([=](const QJsonDocument &message) {
		_delegate->panelWebviewMessage(message);
	});

	raw->setNavigationHandler([=](const QString &uri) {
		return _delegate->panelWebviewNavigationAttempt(uri);
	});

	raw->init(R"(
window.TelegramWebviewProxy = {
postEvent: function(eventType, eventData) {
	if (window.external && window.external.invoke) {
		window.external.invoke(JSON.stringify([eventType, eventData]));
	}
}
};)");

	_widget->showInner(std::move(container));
	return true;
}

void Panel::choosePaymentMethod(const PaymentMethodDetails &method) {
	if (!method.ready) {
		showEditPaymentMethod(method);
		return;
	}
	showBox(Box([=](not_null<Ui::GenericBox*> box) {
		const auto save = [=](int option) {
			if (option) {
				showEditPaymentMethod(method);
			}
		};
		SingleChoiceBox(box, {
			.title = tr::lng_payments_payment_method(),
			.options = { method.title, tr::lng_payments_new_card(tr::now) },
			.initialSelection = 0,
			.callback = save,
		});
	}));
}

void Panel::showEditCard(
		const NativeMethodDetails &native,
		CardField field) {
	Expects(native.supported);

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
		const NativeMethodDetails &native,
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
