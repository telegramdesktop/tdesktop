/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/payments_checkout_process.h"

#include "payments/payments_form.h"
#include "payments/ui/payments_panel.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "storage/storage_account.h"
#include "history/history_item.h"
#include "history/history.h"
#include "data/data_user.h" // UserData::isBot.
#include "boxes/passcode_box.h"
#include "core/local_url_handlers.h" // TryConvertUrlToLocal.
#include "core/file_utilities.h" // File::OpenUrl.
#include "core/core_cloud_password.h" // Core::CloudPasswordState
#include "core/click_handler_types.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "api/api_cloud_password.h"
#include "window/themes/window_theme.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>

namespace Payments {
namespace {

struct SessionProcesses {
	base::flat_map<FullMsgId, std::unique_ptr<CheckoutProcess>> byItem;
	base::flat_map<QString, std::unique_ptr<CheckoutProcess>> bySlug;
	base::flat_map<uint64, std::unique_ptr<CheckoutProcess>> byRandomId;
	base::flat_map<FullMsgId, PaidInvoice> paymentStartedByItem;
	base::flat_map<QString, PaidInvoice> paymentStartedBySlug;
	rpl::lifetime lifetime;
};

base::flat_map<not_null<Main::Session*>, SessionProcesses> Processes;

[[nodiscard]] SessionProcesses &LookupSessionProcesses(
		not_null<Main::Session*> session) {
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

void CheckoutProcess::Start(
		not_null<const HistoryItem*> item,
		Mode mode,
		Fn<void(CheckoutResult)> reactivate,
		Fn<void(NonPanelPaymentForm)> nonPanelPaymentFormProcess) {
	auto &processes = LookupSessionProcesses(&item->history()->session());
	const auto media = item->media();
	const auto invoice = media ? media->invoice() : nullptr;
	if (mode == Mode::Payment && !invoice) {
		return;
	}
	const auto id = (invoice && invoice->receiptMsgId)
		? FullMsgId(item->history()->peer->id, invoice->receiptMsgId)
		: item->fullId();
	if (invoice) {
		mode = invoice->receiptMsgId ? Mode::Receipt : Mode::Payment;
	} else if (mode == Mode::Payment) {
		LOG(("API Error: CheckoutProcess Payment start without invoice."));
		return;
	}
	const auto i = processes.byItem.find(id);
	if (i != end(processes.byItem)) {
		i->second->setReactivateCallback(std::move(reactivate));
		i->second->setNonPanelPaymentFormProcess(
			std::move(nonPanelPaymentFormProcess));
		i->second->requestActivate();
		return;
	}
	const auto j = processes.byItem.emplace(
		id,
		std::make_unique<CheckoutProcess>(
			InvoiceId{ InvoiceMessage{ item->history()->peer, id.msg } },
			mode,
			std::move(reactivate),
			std::move(nonPanelPaymentFormProcess),
			PrivateTag{})).first;
	j->second->requestActivate();
}

void CheckoutProcess::Start(
		not_null<Main::Session*> session,
		const QString &slug,
		Fn<void(CheckoutResult)> reactivate,
		Fn<void(NonPanelPaymentForm)> nonPanelPaymentFormProcess) {
	auto &processes = LookupSessionProcesses(session);
	const auto i = processes.bySlug.find(slug);
	if (i != end(processes.bySlug)) {
		i->second->setReactivateCallback(std::move(reactivate));
		i->second->setNonPanelPaymentFormProcess(
			std::move(nonPanelPaymentFormProcess));
		i->second->requestActivate();
		return;
	}
	const auto j = processes.bySlug.emplace(
		slug,
		std::make_unique<CheckoutProcess>(
			InvoiceId{ InvoiceSlug{ session, slug } },
			Mode::Payment,
			std::move(reactivate),
			std::move(nonPanelPaymentFormProcess),
			PrivateTag{})).first;
	j->second->requestActivate();
}

void CheckoutProcess::Start(
		InvoicePremiumGiftCode giftCodeInvoice,
		Fn<void(CheckoutResult)> reactivate,
		Fn<void(NonPanelPaymentForm)> nonPanelPaymentFormProcess) {
	const auto randomId = giftCodeInvoice.randomId;
	auto id = InvoiceId{ std::move(giftCodeInvoice) };
	auto &processes = LookupSessionProcesses(SessionFromId(id));
	const auto i = processes.byRandomId.find(randomId);
	if (i != end(processes.byRandomId)) {
		i->second->setReactivateCallback(std::move(reactivate));
		i->second->setNonPanelPaymentFormProcess(
			std::move(nonPanelPaymentFormProcess));
		i->second->requestActivate();
		return;
	}
	const auto j = processes.byRandomId.emplace(
		randomId,
		std::make_unique<CheckoutProcess>(
			std::move(id),
			Mode::Payment,
			std::move(reactivate),
			std::move(nonPanelPaymentFormProcess),
			PrivateTag{})).first;
	j->second->requestActivate();
}

void CheckoutProcess::Start(
		InvoiceCredits creditsInvoice,
		Fn<void(CheckoutResult)> reactivate) {
	const auto randomId = creditsInvoice.randomId;
	auto id = InvoiceId{ std::move(creditsInvoice) };
	auto &processes = LookupSessionProcesses(SessionFromId(id));
	const auto i = processes.byRandomId.find(randomId);
	if (i != end(processes.byRandomId)) {
		i->second->setReactivateCallback(std::move(reactivate));
		i->second->requestActivate();
		return;
	}
	const auto j = processes.byRandomId.emplace(
		randomId,
		std::make_unique<CheckoutProcess>(
			std::move(id),
			Mode::Payment,
			std::move(reactivate),
			nullptr,
			PrivateTag{})).first;
	j->second->requestActivate();
}

void CheckoutProcess::Start(
		InvoiceStarGift giftInvoice,
		Fn<void(CheckoutResult)> reactivate,
		Fn<void(NonPanelPaymentForm)> nonPanelPaymentFormProcess) {
	const auto randomId = giftInvoice.randomId;
	auto id = InvoiceId{ std::move(giftInvoice) };
	auto &processes = LookupSessionProcesses(SessionFromId(id));
	const auto i = processes.byRandomId.find(randomId);
	if (i != end(processes.byRandomId)) {
		i->second->setReactivateCallback(std::move(reactivate));
		i->second->setNonPanelPaymentFormProcess(
			std::move(nonPanelPaymentFormProcess));
		return;
	}
	processes.byRandomId.emplace(
		randomId,
		std::make_unique<CheckoutProcess>(
			std::move(id),
			Mode::Payment,
			std::move(reactivate),
			std::move(nonPanelPaymentFormProcess),
			PrivateTag{}));
}

std::optional<PaidInvoice> CheckoutProcess::InvoicePaid(
		not_null<const HistoryItem*> item) {
	const auto session = &item->history()->session();
	const auto itemId = item->fullId();
	const auto i = Processes.find(session);
	if (i == end(Processes)) {
		return std::nullopt;
	}
	const auto k = i->second.paymentStartedByItem.find(itemId);
	if (k == end(i->second.paymentStartedByItem)) {
		return std::nullopt;
	}
	const auto result = k->second;
	i->second.paymentStartedByItem.erase(k);

	const auto j = i->second.byItem.find(itemId);
	if (j != end(i->second.byItem)) {
		j->second->closeAndReactivate(CheckoutResult::Paid);
	} else if (i->second.paymentStartedByItem.empty()
		&& i->second.byItem.empty()
		&& i->second.paymentStartedBySlug.empty()
		&& i->second.bySlug.empty()
		&& i->second.byRandomId.empty()) {
		Processes.erase(i);
	}
	return result;
}

std::optional<PaidInvoice> CheckoutProcess::InvoicePaid(
		not_null<Main::Session*> session,
		const QString &slug) {
	const auto i = Processes.find(session);
	if (i == end(Processes)) {
		return std::nullopt;
	}
	const auto k = i->second.paymentStartedBySlug.find(slug);
	if (k == end(i->second.paymentStartedBySlug)) {
		return std::nullopt;
	}
	const auto result = k->second;
	i->second.paymentStartedBySlug.erase(k);

	const auto j = i->second.bySlug.find(slug);
	if (j != end(i->second.bySlug)) {
		j->second->closeAndReactivate(CheckoutResult::Paid);
	} else if (i->second.paymentStartedByItem.empty()
		&& i->second.byItem.empty()
		&& i->second.paymentStartedBySlug.empty()
		&& i->second.bySlug.empty()
		&& i->second.byRandomId.empty()) {
		Processes.erase(i);
	}
	return result;
}

void CheckoutProcess::ClearAll() {
	Processes.clear();
}

void CheckoutProcess::RegisterPaymentStart(
		not_null<CheckoutProcess*> process,
		PaidInvoice info) {
	const auto i = Processes.find(process->_session);
	Assert(i != end(Processes));
	for (const auto &[itemId, itemProcess] : i->second.byItem) {
		if (itemProcess.get() == process) {
			i->second.paymentStartedByItem.emplace(itemId, info);
			return;
		}
	}
	for (const auto &[slug, itemProcess] : i->second.bySlug) {
		if (itemProcess.get() == process) {
			i->second.paymentStartedBySlug.emplace(slug, info);
			return;
		}
	}
	for (const auto &[randomId, itemProcess] : i->second.byRandomId) {
		if (itemProcess.get() == process) {
			return;
		}
	}
}

void CheckoutProcess::UnregisterPaymentStart(
		not_null<CheckoutProcess*> process) {
	const auto i = Processes.find(process->_session);
	if (i == end(Processes)) {
		return;
	}
	for (const auto &[itemId, itemProcess] : i->second.byItem) {
		if (itemProcess.get() == process) {
			i->second.paymentStartedByItem.remove(itemId);
			break;
		}
	}
	for (const auto &[slug, itemProcess] : i->second.bySlug) {
		if (itemProcess.get() == process) {
			i->second.paymentStartedBySlug.remove(slug);
			break;
		}
	}
	for (const auto &[randomId, itemProcess] : i->second.byRandomId) {
		if (itemProcess.get() == process) {
			break;
		}
	}
	if (i->second.paymentStartedByItem.empty()
		&& i->second.byItem.empty()
		&& i->second.paymentStartedBySlug.empty()
		&& i->second.bySlug.empty()
		&& i->second.byRandomId.empty()) {
		Processes.erase(i);
	}
}

CheckoutProcess::CheckoutProcess(
	InvoiceId id,
	Mode mode,
	Fn<void(CheckoutResult)> reactivate,
	Fn<void(NonPanelPaymentForm)> nonPanelPaymentFormProcess,
	PrivateTag)
: _session(SessionFromId(id))
, _form(std::make_unique<Form>(id, (mode == Mode::Receipt)))
, _panel(std::make_unique<Ui::Panel>(panelDelegate()))
, _reactivate(std::move(reactivate))
, _nonPanelPaymentFormProcess(std::move(nonPanelPaymentFormProcess)) {
	_form->updates(
	) | rpl::start_with_next([=](const FormUpdate &update) {
		handleFormUpdate(update);
	}, _lifetime);

	_panel->savedMethodChosen(
	) | rpl::start_with_next([=](QString id) {
		_form->chooseSavedMethod(id);
	}, _panel->lifetime());

	_panel->backRequests(
	) | rpl::start_with_next([=] {
		panelCancelEdit();
	}, _panel->lifetime());
	if (!_nonPanelPaymentFormProcess) {
		showForm();
	}
	_panel->toggleProgress(true);

	if (mode == Mode::Payment) {
		_session->api().cloudPassword().state(
		) | rpl::start_with_next([=](const Core::CloudPasswordState &state) {
			_form->setHasPassword(state.hasPassword);
		}, _lifetime);
	}
}

CheckoutProcess::~CheckoutProcess() {
}

void CheckoutProcess::setReactivateCallback(
		Fn<void(CheckoutResult)> reactivate) {
	_reactivate = std::move(reactivate);
}

void CheckoutProcess::setNonPanelPaymentFormProcess(
		Fn<void(NonPanelPaymentForm)> callback) {
	_nonPanelPaymentFormProcess = std::move(callback);
}

void CheckoutProcess::requestActivate() {
	if (!_nonPanelPaymentFormProcess) {
		_panel->requestActivate();
	}
}

not_null<Ui::PanelDelegate*> CheckoutProcess::panelDelegate() {
	return static_cast<PanelDelegate*>(this);
}

void CheckoutProcess::handleFormUpdate(const FormUpdate &update) {
	v::match(update, [&](const ToggleProgress &data) {
		_panel->toggleProgress(data.shown);
	}, [&](const FormReady &) {
		performInitialSilentValidation();
		if (!_initialSilentValidation) {
			showForm();
		}
		if (!_form->paymentMethod().savedCredentials.empty()) {
			_session->api().cloudPassword().reload();
		}
	}, [&](const ThumbnailUpdated &data) {
		_panel->updateFormThumbnail(data.thumbnail);
	}, [&](const ValidateFinished &) {
		if (_initialSilentValidation) {
			_initialSilentValidation = false;
		}
		showForm();
		const auto submitted = (_submitState == SubmitState::Validating);
		_submitState = SubmitState::Validated;
		if (submitted) {
			panelSubmit();
		}
	}, [&](const PaymentMethodUpdate &data) {
		showForm();
		if (data.requestNewPassword) {
			requestSetPassword();
		}
	}, [&](const TmpPasswordRequired &) {
		UnregisterPaymentStart(this);
		_submitState = SubmitState::Validated;
		requestPassword();
	}, [&](const BotTrustRequired &data) {
		UnregisterPaymentStart(this);
		_submitState = SubmitState::Validated;
		_panel->showWarning(data.bot->name(), data.provider->name());
		if (const auto box = _enterPasswordBox.get()) {
			box->closeBox();
		}
	}, [&](const VerificationNeeded &data) {
		auto bottomText = tr::lng_payments_processed_by(
			lt_provider,
			rpl::single(_form->invoice().provider));
		_sendFormFailed = false;
		_sendFormPending = true;
		if (!_panel->showWebview(data.url, false, std::move(bottomText))) {
			File::OpenUrl(data.url);
			close();
		}
	}, [&](const PaymentFinished &data) {
		const auto weak = base::make_weak(this);
		_session->api().applyUpdates(data.updates);
		if (weak) {
			closeAndReactivate(CheckoutResult::Paid);
		}
	}, [&](const CreditsPaymentStarted &data) {
		if (_nonPanelPaymentFormProcess) {
			_nonPanelPaymentFormProcess(
				std::make_shared<CreditsFormData>(data.data));
			close();
		}
	}, [&](const CreditsReceiptReady &data) {
		if (_nonPanelPaymentFormProcess) {
			_nonPanelPaymentFormProcess(
				std::make_shared<CreditsReceiptData>(data.data));
			close();
		}
	}, [&](const Error &error) {
		handleError(error);
	});
}

void CheckoutProcess::handleError(const Error &error) {
	const auto showToast = [&](TextWithEntities &&text) {
		_panel->requestActivate();
		_panel->showToast(std::move(text));
	};
	const auto &id = error.id;
	switch (error.type) {
	case Error::Type::Form:
		if (id == u"INVOICE_ALREADY_PAID"_q) {
			_panel->showCriticalError({
				tr::lng_payments_already_paid(tr::now)
			});
		} else if (true
			|| id == u"PROVIDER_ACCOUNT_INVALID"_q
			|| id == u"PROVIDER_ACCOUNT_TIMEOUT"_q) {
			_panel->showCriticalError({ "Error: " + id });
		}
		break;
	case Error::Type::Validate: {
		if (_submitState == SubmitState::Validating
			|| _submitState == SubmitState::Validated) {
			_submitState = SubmitState::None;
		}
		if (_initialSilentValidation) {
			_initialSilentValidation = false;
			showForm();
			return;
		}
		using InfoField = Ui::InformationField;
		using CardField = Ui::CardField;
		if (id == u"REQ_INFO_NAME_INVALID"_q) {
			showInformationError(InfoField::Name);
		} else if (id == u"REQ_INFO_EMAIL_INVALID"_q) {
			showInformationError(InfoField::Email);
		} else if (id == u"REQ_INFO_PHONE_INVALID"_q) {
			showInformationError(InfoField::Phone);
		} else if (id == u"ADDRESS_STREET_LINE1_INVALID"_q) {
			showInformationError(InfoField::ShippingStreet);
		} else if (id == u"ADDRESS_CITY_INVALID"_q) {
			showInformationError(InfoField::ShippingCity);
		} else if (id == u"ADDRESS_STATE_INVALID"_q) {
			showInformationError(InfoField::ShippingState);
		} else if (id == u"ADDRESS_COUNTRY_INVALID"_q) {
			showInformationError(InfoField::ShippingCountry);
		} else if (id == u"ADDRESS_POSTCODE_INVALID"_q) {
			showInformationError(InfoField::ShippingPostcode);
		} else if (id == u"LOCAL_CARD_NUMBER_INVALID"_q) {
			showCardError(CardField::Number);
		} else if (id == u"LOCAL_CARD_EXPIRE_DATE_INVALID"_q) {
			showCardError(CardField::ExpireDate);
		} else if (id == u"LOCAL_CARD_CVC_INVALID"_q) {
			showCardError(CardField::Cvc);
		} else if (id == u"LOCAL_CARD_HOLDER_NAME_INVALID"_q) {
			showCardError(CardField::Name);
		} else if (id == u"LOCAL_CARD_BILLING_COUNTRY_INVALID"_q) {
			showCardError(CardField::AddressCountry);
		} else if (id == u"LOCAL_CARD_BILLING_ZIP_INVALID"_q) {
			showCardError(CardField::AddressZip);
		} else if (id == u"SHIPPING_BOT_TIMEOUT"_q) {
			showToast({ "Error: Bot Timeout!" });
		} else if (id == u"SHIPPING_NOT_AVAILABLE"_q) {
			showToast({ tr::lng_payments_shipping_not_available(tr::now) });
		} else {
			showToast({ "Error: " + id });
		}
	} break;
	case Error::Type::Stripe: {
		using Field = Ui::CardField;
		if (id == u"InvalidNumber"_q || id == u"IncorrectNumber"_q) {
			showCardError(Field::Number);
		} else if (id == u"InvalidCVC"_q || id == u"IncorrectCVC"_q) {
			showCardError(Field::Cvc);
		} else if (id == u"InvalidExpiryMonth"_q
			|| id == u"InvalidExpiryYear"_q
			|| id == u"ExpiredCard"_q) {
			showCardError(Field::ExpireDate);
		} else if (id == u"CardDeclined"_q) {
			showToast({ tr::lng_payments_card_declined(tr::now) });
		} else if (id == u"ProcessingError"_q) {
			showToast({ "Sorry, a processing error occurred." });
		} else {
			showToast({ "Stripe Error: " + id });
		}
	} break;
	case Error::Type::SmartGlocal: {
		showToast({ "SmartGlocal Error: " + id });
	} break;
	case Error::Type::TmpPassword:
		if (const auto box = _enterPasswordBox.get()) {
			if (!box->handleCustomCheckError(id)) {
				showToast({ "Error: Could not generate tmp password." });
			}
		}
		break;
	case Error::Type::Send:
		_sendFormFailed = true;
		if (const auto box = _enterPasswordBox.get()) {
			box->closeBox();
		}
		if (_submitState == SubmitState::Finishing) {
			UnregisterPaymentStart(this);
			_submitState = SubmitState::Validated;
		}
		if (id == u"INVOICE_ALREADY_PAID"_q) {
			showToast({ tr::lng_payments_already_paid(tr::now) });
		} else if (id == u"PAYMENT_FAILED"_q) {
			showToast({ tr::lng_payments_payment_failed(tr::now) });
		} else if (id == u"BOT_PRECHECKOUT_FAILED"_q) {
			showToast({ tr::lng_payments_precheckout_failed(tr::now) });
		} else if (id == u"BOT_PRECHECKOUT_TIMEOUT"_q) {
			showToast({ tr::lng_payments_precheckout_timeout(tr::now) });
		} else if (id == u"REQUESTED_INFO_INVALID"_q
			|| id == u"SHIPPING_OPTION_INVALID"_q
			|| id == u"PAYMENT_CREDENTIALS_INVALID"_q
			|| id == u"PAYMENT_CREDENTIALS_ID_INVALID"_q) {
			showToast({ tr::lng_payments_payment_failed(tr::now) });
			showToast({ "Error: " + id + ". Your card has not been billed." });
		} else if (id == u"TMP_PASSWORD_INVALID"_q) {
			requestPassword();
		} else {
			showToast({ "Error: " + id });
		}
		break;
	default: Unexpected("Error type in CheckoutProcess::handleError.");
	}
}

void CheckoutProcess::panelRequestClose() {
	if (_form->hasChanges()) {
		_panel->showCloseConfirm();
	} else {
		panelCloseSure();
	}
}

void CheckoutProcess::panelCloseSure() {
	closeAndReactivate(_sendFormFailed
		? CheckoutResult::Failed
		: _sendFormPending
		? CheckoutResult::Pending
		: CheckoutResult::Cancelled);
}

void CheckoutProcess::closeAndReactivate(CheckoutResult result) {
	const auto reactivate = std::move(_reactivate);
	close();
	if (reactivate) {
		reactivate(result);
	}
}

void CheckoutProcess::close() {
	const auto i = Processes.find(_session);
	if (i == end(Processes)) {
		return;
	}
	auto &entry = i->second;
	const auto j = ranges::find(entry.byItem, this, [](const auto &pair) {
		return pair.second.get();
	});
	if (j != end(entry.byItem)) {
		entry.byItem.erase(j);
	}
	const auto k = ranges::find(entry.bySlug, this, [](const auto &pair) {
		return pair.second.get();
	});
	if (k != end(entry.bySlug)) {
		entry.bySlug.erase(k);
	}
	const auto l = ranges::find(
		entry.byRandomId,
		this,
		[](const auto &pair) { return pair.second.get(); });
	if (l != end(entry.byRandomId)) {
		entry.byRandomId.erase(l);
	}
	if (entry.byItem.empty()
		&& entry.bySlug.empty()
		&& i->second.byRandomId.empty()
		&& entry.paymentStartedByItem.empty()
		&& entry.paymentStartedBySlug.empty()) {
		Processes.erase(i);
	}
}

void CheckoutProcess::panelSubmit() {
	if (_form->invoice().receipt.paid) {
		closeAndReactivate(CheckoutResult::Paid);
		return;
	} else if (_submitState == SubmitState::Validating
		|| _submitState == SubmitState::Finishing) {
		return;
	}
	const auto &method = _form->paymentMethod();
	const auto &invoice = _form->invoice();
	const auto &options = _form->shippingOptions();
	if (!options.list.empty() && options.selectedId.isEmpty()) {
		chooseShippingOption();
	} else if (_submitState != SubmitState::Validated
		&& options.list.empty()
		&& (invoice.isShippingAddressRequested
			|| invoice.isNameRequested
			|| invoice.isEmailRequested
			|| invoice.isPhoneRequested)) {
		_submitState = SubmitState::Validating;
		_form->validateInformation(_form->information());
	} else if (!method.newCredentials
		&& method.savedCredentialsIndex >= method.savedCredentials.size()) {
		editPaymentMethod();
	} else if (!invoice.termsUrl.isEmpty()
		&& !_form->details().termsAccepted) {
		_panel->requestTermsAcceptance(
			_form->details().termsBotUsername,
			invoice.termsUrl,
			invoice.isRecurring);
	} else {
		RegisterPaymentStart(this, { _form->invoice().cover.title });
		_submitState = SubmitState::Finishing;
		_form->submit();
	}
}

void CheckoutProcess::panelTrustAndSubmit() {
	_form->trustBot();
	panelSubmit();
}

void CheckoutProcess::panelAcceptTermsAndSubmit() {
	_form->acceptTerms();
	panelSubmit();
}

void CheckoutProcess::panelWebviewMessage(
		const QJsonDocument &message,
		bool saveInformation) {
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
	crl::on_main(this, [=] {
		_form->setPaymentCredentials(NewCredentials{
			.title = title,
			.data = QJsonDocument(
				credentials.toObject()
			).toJson(QJsonDocument::Compact),
			.saveOnServer = saveInformation,
		});
	});
}

std::optional<QDate> CheckoutProcess::panelOverrideExpireDateThreshold() {
	return _form->overrideExpireDateThreshold();
}

bool CheckoutProcess::panelWebviewNavigationAttempt(const QString &uri) {
	if (Core::TryConvertUrlToLocal(uri) == uri) {
		return true;
	}
	// #TODO payments
	crl::on_main(this, [=] { closeAndReactivate(CheckoutResult::Paid); });
	return false;
}

void CheckoutProcess::panelCancelEdit() {
	if (_submitState != SubmitState::None
		&& _submitState != SubmitState::Validated) {
		return;
	}
	showForm();
}

void CheckoutProcess::panelEditPaymentMethod() {
	if (_submitState != SubmitState::None
		&& _submitState != SubmitState::Validated) {
		return;
	}
	editPaymentMethod();
}

void CheckoutProcess::panelValidateCard(
		Ui::UncheckedCardDetails data,
		bool saveInformation) {
	_form->validateCard(data, saveInformation);
}

void CheckoutProcess::panelEditShippingInformation() {
	showEditInformation(Ui::InformationField::ShippingStreet);
}

void CheckoutProcess::panelEditName() {
	showEditInformation(Ui::InformationField::Name);
}

void CheckoutProcess::panelEditEmail() {
	showEditInformation(Ui::InformationField::Email);
}

void CheckoutProcess::panelEditPhone() {
	showEditInformation(Ui::InformationField::Phone);
}

void CheckoutProcess::showForm() {
	_panel->showForm(
		_form->invoice(),
		_form->information(),
		_form->paymentMethod().ui,
		_form->shippingOptions());
	if (_nonPanelPaymentFormProcess && !_realFormNotified) {
		_realFormNotified = true;
		const auto weak = base::make_weak(_panel.get());
		_nonPanelPaymentFormProcess(RealFormPresentedNotification());
		if (weak) {
			requestActivate();
		}
	}
}

void CheckoutProcess::showEditInformation(Ui::InformationField field) {
	if (_submitState != SubmitState::None
		&& _submitState != SubmitState::Validated) {
		return;
	}
	_panel->showEditInformation(
		_form->invoice(),
		_form->information(),
		field);
}

void CheckoutProcess::showInformationError(Ui::InformationField field) {
	Expects(_submitState != SubmitState::Validated);

	if (_submitState != SubmitState::None) {
		return;
	}
	_panel->showInformationError(
		_form->invoice(),
		_form->information(),
		field);
}

void CheckoutProcess::showCardError(Ui::CardField field) {
	if (_submitState != SubmitState::None
		&& _submitState != SubmitState::Validated) {
		return;
	}
	_panel->showCardError(_form->paymentMethod().ui.native, field);
}

void CheckoutProcess::chooseShippingOption() {
	_panel->chooseShippingOption(_form->shippingOptions());
}

void CheckoutProcess::chooseTips() {
	_panel->chooseTips(_form->invoice());
}

void CheckoutProcess::editPaymentMethod() {
	_panel->choosePaymentMethod(_form->paymentMethod().ui);
}

void CheckoutProcess::requestSetPassword() {
	_session->api().cloudPassword().reload();
	_panel->askSetPassword();
}

void CheckoutProcess::requestPassword() {
	getPasswordState([=](const Core::CloudPasswordState &state) {
		auto fields = PasscodeBox::CloudFields::From(state);
		fields.customTitle = tr::lng_payments_password_title();
		const auto &method = _form->paymentMethod();
		const auto &list = method.savedCredentials;
		const auto index = method.savedCredentialsIndex;
		fields.customDescription = tr::lng_payments_password_description(
			tr::now,
			lt_card,
			(index < list.size()) ? list[index].title : QString());
		fields.customSubmitButton = tr::lng_payments_password_submit();
		fields.customCheckCallback = [=](
				const Core::CloudPasswordResult &result,
				base::weak_qptr<PasscodeBox> box) {
			_enterPasswordBox = box;
			_form->submit(result);
		};
		_panel->showBox(Box<PasscodeBox>(_session, fields));
	});
}

void CheckoutProcess::panelSetPassword() {
	getPasswordState([=](const Core::CloudPasswordState &state) {
		if (state.hasPassword) {
			return;
		}
		auto owned = Box<PasscodeBox>(
			_session,
			PasscodeBox::CloudFields::From(state));
		const auto box = owned.data();

		rpl::merge(
			box->newPasswordSet() | rpl::to_empty,
			box->passwordReloadNeeded()
		) | rpl::start_with_next([=] {
			_session->api().cloudPassword().reload();
		}, box->lifetime());

		box->clearUnconfirmedPassword(
		) | rpl::start_with_next([=] {
			_session->api().cloudPassword().clearUnconfirmedPassword();
		}, box->lifetime());

		_panel->showBox(std::move(owned));
	});
}

void CheckoutProcess::panelOpenUrl(const QString &url) {
	File::OpenUrl(url);
}

void CheckoutProcess::getPasswordState(
		Fn<void(const Core::CloudPasswordState&)> callback) {
	Expects(callback != nullptr);

	if (_gettingPasswordState) {
		return;
	}
	_session->api().cloudPassword().state(
	) | rpl::start_with_next([=](const Core::CloudPasswordState &state) {
		_gettingPasswordState.destroy();
		callback(state);
	}, _gettingPasswordState);
}

void CheckoutProcess::panelChooseShippingOption() {
	if (_submitState != SubmitState::None
		&& _submitState != SubmitState::Validated) {
		return;
	}
	chooseShippingOption();
}

void CheckoutProcess::panelChangeShippingOption(const QString &id) {
	_form->setShippingOption(id);
	showForm();
}

void CheckoutProcess::panelChooseTips() {
	if (_submitState != SubmitState::None
		&& _submitState != SubmitState::Validated) {
		return;
	}
	chooseTips();
}

void CheckoutProcess::panelChangeTips(int64 value) {
	_form->setTips(value);
	showForm();
}

void CheckoutProcess::panelValidateInformation(
		Ui::RequestedInformation data) {
	if (_submitState == SubmitState::Validated) {
		_submitState = SubmitState::None;
	}
	_form->validateInformation(data);
}

void CheckoutProcess::panelShowBox(object_ptr<Ui::BoxContent> box) {
	_panel->showBox(std::move(box));
}

QVariant CheckoutProcess::panelClickHandlerContext() {
	return QVariant::fromValue(ClickHandlerContext{
		.show = _panel->uiShow(),
	});
}

void CheckoutProcess::performInitialSilentValidation() {
	const auto &invoice = _form->invoice();
	const auto &saved = _form->information();
	if (invoice.receipt
		|| (invoice.isNameRequested && saved.name.isEmpty())
		|| (invoice.isEmailRequested && saved.email.isEmpty())
		|| (invoice.isPhoneRequested && saved.phone.isEmpty())
		|| (invoice.isShippingAddressRequested && !saved.shippingAddress)) {
		return;
	}
	_initialSilentValidation = true;
	_form->validateInformation(saved);
}

Webview::StorageId CheckoutProcess::panelWebviewStorageId() {
	return _session->local().resolveStorageIdOther();
}

Webview::ThemeParams CheckoutProcess::panelWebviewThemeParams() {
	return Window::Theme::WebViewParams();
}

} // namespace Payments
