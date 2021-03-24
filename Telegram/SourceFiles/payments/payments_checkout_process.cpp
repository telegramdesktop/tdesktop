/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/payments_checkout_process.h"

#include "payments/payments_form.h"
#include "payments/ui/payments_panel.h"
#include "payments/ui/payments_webview.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "storage/storage_domain.h"
#include "history/history_item.h"
#include "history/history.h"
#include "core/local_url_handlers.h" // TryConvertUrlToLocal.
#include "apiwrap.h"

// #TODO payments errors
#include "mainwindow.h"
#include "ui/toasts/common_toasts.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>

namespace Payments {
namespace {

struct SessionProcesses {
	base::flat_map<FullMsgId, std::unique_ptr<CheckoutProcess>> map;
	rpl::lifetime lifetime;
};

base::flat_map<not_null<Main::Session*>, SessionProcesses> Processes;

[[nodiscard]] SessionProcesses &LookupSessionProcesses(
		not_null<const HistoryItem*> item) {
	const auto session = &item->history()->session();
	const auto i = Processes.find(session);
	if (i != end(Processes)) {
		return i->second;
	}
	const auto j = Processes.emplace(session).first;
	auto &result = j->second;
	session->account().sessionChanges(
	) | rpl::start_with_next([=] {
		Processes.erase(session);
	}, result.lifetime);
	return result;
}

} // namespace

void CheckoutProcess::Start(not_null<const HistoryItem*> item) {
	auto &processes = LookupSessionProcesses(item);
	const auto session = &item->history()->session();
	const auto id = item->fullId();
	const auto i = processes.map.find(id);
	if (i != end(processes.map)) {
		i->second->requestActivate();
		return;
	}
	const auto j = processes.map.emplace(
		id,
		std::make_unique<CheckoutProcess>(session, id, PrivateTag{})).first;
	j->second->requestActivate();
}

CheckoutProcess::CheckoutProcess(
	not_null<Main::Session*> session,
	FullMsgId itemId,
	PrivateTag)
: _session(session)
, _form(std::make_unique<Form>(session, itemId))
, _panel(std::make_unique<Ui::Panel>(panelDelegate())) {
	_form->updates(
	) | rpl::start_with_next([=](const FormUpdate &update) {
		handleFormUpdate(update);
	}, _lifetime);
	_panel->backRequests(
	) | rpl::start_with_next([=] {
		showForm();
	}, _panel->lifetime());
}

CheckoutProcess::~CheckoutProcess() {
}

void CheckoutProcess::requestActivate() {
	_panel->requestActivate();
}

not_null<Ui::PanelDelegate*> CheckoutProcess::panelDelegate() {
	return static_cast<PanelDelegate*>(this);
}

void CheckoutProcess::handleFormUpdate(const FormUpdate &update) {
	v::match(update.data, [&](const FormReady &) {
		showForm();
	}, [&](const ValidateFinished &) {
		showForm();
		if (_submitState == SubmitState::Validation) {
			_submitState = SubmitState::Validated;
			panelSubmit();
		}
	}, [&](const VerificationNeeded &info) {
		if (_webviewWindow) {
			_webviewWindow->navigate(info.url);
		} else {
			_webviewWindow = std::make_unique<Ui::WebviewWindow>(
				webviewDataPath(),
				info.url,
				panelDelegate());
			if (!_webviewWindow->shown()) {
				// #TODO payments errors
			}
		}
	}, [&](const PaymentFinished &result) {
		const auto weak = base::make_weak(this);
		_session->api().applyUpdates(result.updates);
		if (weak) {
			panelCloseSure();
		}
	}, [&](const Error &error) {
		handleError(error);
	});
}

void CheckoutProcess::handleError(const Error &error) {
	const auto showToast = [&](const TextWithEntities &text) {
		if (_panel) {
			_panel->requestActivate();
			_panel->showToast(text);
		} else {
			App::wnd()->activate();
			Ui::ShowMultilineToast({ .text = text });
		}
	};
	const auto &id = error.id;
	switch (error.type) {
	case Error::Type::Form:
		if (id == u"INVOICE_ALREADY_PAID"_q) {
			showToast({ "Already Paid!" }); // #TODO payments errors message
		} else if (true
			|| id == u"PROVIDER_ACCOUNT_INVALID"_q
			|| id == u"PROVIDER_ACCOUNT_TIMEOUT"_q) {
			showToast({ "Error: " + id });
		}
		break;
	case Error::Type::Validate:
		if (_submitState == SubmitState::Validation) {
			_submitState = SubmitState::None;
		}
		if (id == u"REQ_INFO_NAME_INVALID"_q) {
			showEditError(Ui::EditField::Name);
		} else if (id == u"REQ_INFO_EMAIL_INVALID"_q) {
			showEditError(Ui::EditField::Email);
		} else if (id == u"REQ_INFO_PHONE_INVALID"_q) {
			showEditError(Ui::EditField::Phone);
		} else if (id == u"ADDRESS_STREET_LINE1_INVALID"_q) {
			showEditError(Ui::EditField::ShippingStreet);
		} else if (id == u"ADDRESS_CITY_INVALID"_q) {
			showEditError(Ui::EditField::ShippingCity);
		} else if (id == u"ADDRESS_STATE_INVALID"_q) {
			showEditError(Ui::EditField::ShippingState);
		} else if (id == u"ADDRESS_COUNTRY_INVALID"_q) {
			showEditError(Ui::EditField::ShippingCountry);
		} else if (id == u"ADDRESS_POSTCODE_INVALID"_q) {
			showEditError(Ui::EditField::ShippingPostcode);
		} else if (id == u"SHIPPING_BOT_TIMEOUT"_q) {
			showToast({ "Error: Bot Timeout!" }); // #TODO payments errors message
		} else if (id == u"SHIPPING_NOT_AVAILABLE"_q) {
			showToast({ "Error: Shipping to the selected country is not available!" }); // #TODO payments errors message
		} else {
			showToast({ "Error: " + id });
		}
		break;
	case Error::Type::Send:
		if (_submitState == SubmitState::Finishing) {
			_submitState = SubmitState::None;
		}
		if (id == u"PAYMENT_FAILED"_q) {
			showToast({ "Error: Payment Failed. Your card has not been billed." }); // #TODO payments errors message
		} else if (id == u"BOT_PRECHECKOUT_FAILED"_q) {
			showToast({ "Error: PreCheckout Failed. Your card has not been billed." }); // #TODO payments errors message
		} else if (id == u"INVOICE_ALREADY_PAID"_q) {
			showToast({ "Already Paid!" }); // #TODO payments errors message
		} else if (id == u"REQUESTED_INFO_INVALID"_q
			|| id == u"SHIPPING_OPTION_INVALID"_q
			|| id == u"PAYMENT_CREDENTIALS_INVALID"_q
			|| id == u"PAYMENT_CREDENTIALS_ID_INVALID"_q) {
			showToast({ "Error: " + id + ". Your card has not been billed." });
		}
		break;
	default: Unexpected("Error type in CheckoutProcess::handleError.");
	}
}

void CheckoutProcess::panelRequestClose() {
	panelCloseSure(); // #TODO payments confirmation
}

void CheckoutProcess::panelCloseSure() {
	const auto i = Processes.find(_session);
	if (i == end(Processes)) {
		return;
	}
	const auto j = ranges::find(i->second.map, this, [](const auto &pair) {
		return pair.second.get();
	});
	if (j == end(i->second.map)) {
		return;
	}
	i->second.map.erase(j);
	if (i->second.map.empty()) {
		Processes.erase(i);
	}
}

void CheckoutProcess::panelSubmit() {
	if (_submitState == SubmitState::Validation
		|| _submitState == SubmitState::Finishing) {
		return;
	}
	const auto &invoice = _form->invoice();
	const auto &options = _form->shippingOptions();
	if (!options.list.empty() && options.selectedId.isEmpty()) {
		chooseShippingOption();
		return;
	} else if (_submitState != SubmitState::Validated
		&& options.list.empty()
		&& (invoice.isShippingAddressRequested
			|| invoice.isNameRequested
			|| invoice.isEmailRequested
			|| invoice.isPhoneRequested)) {
		_submitState = SubmitState::Validation;
		_form->validateInformation(_form->savedInformation());
		return;
	}
	_submitState = SubmitState::Finishing;
	_webviewWindow = std::make_unique<Ui::WebviewWindow>(
		webviewDataPath(),
		_form->details().url,
		panelDelegate());
	if (!_webviewWindow->shown()) {
		// #TODO payments errors
	}
}

void CheckoutProcess::panelWebviewMessage(const QJsonDocument &message) {
	if (!message.isArray()) {
		LOG(("Payments Error: "
			"Not an array received in buy_callback arguments."));
		return;
	}
	const auto list = message.array();
	if (list.at(0).toString() != "payment_form_submit") {
		return;
	} else if (!list.at(1).isString()) {
		LOG(("Payments Error: "
			"Not a string received in buy_callback result."));
		return;
	}

	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(
		list.at(1).toString().toUtf8(),
		&error);
	if (error.error != QJsonParseError::NoError) {
		LOG(("Payments Error: "
			"Failed to parse buy_callback arguments, error: %1."
			).arg(error.errorString()));
		return;
	} else if (!document.isObject()) {
		LOG(("Payments Error: "
			"Not an object decoded in buy_callback result."));
		return;
	}
	const auto root = document.object();
	const auto title = root.value("title").toString();
	const auto credentials = root.value("credentials");
	if (!credentials.isObject()) {
		LOG(("Payments Error: "
			"Not an object received in payment credentials."));
		return;
	}
	const auto serializedCredentials = QJsonDocument(
		credentials.toObject()
	).toJson(QJsonDocument::Compact);

	_form->send(serializedCredentials);
}

bool CheckoutProcess::panelWebviewNavigationAttempt(const QString &uri) {
	if (Core::TryConvertUrlToLocal(uri) == uri) {
		return true;
	}
	panelCloseSure();
	App::wnd()->activate();
	return false;
}

void CheckoutProcess::panelEditShippingInformation() {
	showEditInformation(Ui::EditField::ShippingStreet);
}

void CheckoutProcess::panelEditName() {
	showEditInformation(Ui::EditField::Name);
}

void CheckoutProcess::panelEditEmail() {
	showEditInformation(Ui::EditField::Email);
}

void CheckoutProcess::panelEditPhone() {
	showEditInformation(Ui::EditField::Phone);
}

void CheckoutProcess::showForm() {
	_panel->showForm(
		_form->invoice(),
		_form->savedInformation(),
		_form->shippingOptions());
}

void CheckoutProcess::showEditInformation(Ui::EditField field) {
	if (_submitState != SubmitState::None) {
		return;
	}
	_panel->showEditInformation(
		_form->invoice(),
		_form->savedInformation(),
		field);
}

void CheckoutProcess::showEditError(Ui::EditField field) {
	if (_submitState != SubmitState::None) {
		return;
	}
	_panel->showEditError(
		_form->invoice(),
		_form->savedInformation(),
		field);
}

void CheckoutProcess::chooseShippingOption() {
	_panel->chooseShippingOption(_form->shippingOptions());
}

void CheckoutProcess::panelChooseShippingOption() {
	if (_submitState != SubmitState::None) {
		return;
	}
	chooseShippingOption();
}

void CheckoutProcess::panelChangeShippingOption(const QString &id) {
	_form->setShippingOption(id);
	showForm();
}

void CheckoutProcess::panelValidateInformation(
		Ui::RequestedInformation data) {
	_form->validateInformation(data);
}

void CheckoutProcess::panelShowBox(object_ptr<Ui::BoxContent> box) {
	_panel->showBox(std::move(box));
}

QString CheckoutProcess::webviewDataPath() const {
	return _session->domain().local().webviewDataPath();
}

} // namespace Payments
