/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/payments_form.h"

#include "main/main_session.h"
#include "data/data_session.h"
#include "apiwrap.h"

namespace Payments {
namespace {

[[nodiscard]] Ui::Address ParseAddress(const MTPPostAddress &address) {
	return address.match([](const MTPDpostAddress &data) {
		return Ui::Address{
			.address1 = qs(data.vstreet_line1()),
			.address2 = qs(data.vstreet_line2()),
			.city = qs(data.vcity()),
			.state = qs(data.vstate()),
			.countryIso2 = qs(data.vcountry_iso2()),
			.postCode = qs(data.vpost_code()),
		};
	});
}

} // namespace

Form::Form(not_null<Main::Session*> session, FullMsgId itemId)
: _session(session)
, _msgId(itemId.msg) {
	requestForm();
}

void Form::requestForm() {
	_session->api().request(MTPpayments_GetPaymentForm(
		MTP_int(_msgId)
	)).done([=](const MTPpayments_PaymentForm &result) {
		result.match([&](const auto &data) {
			processForm(data);
		});
	}).fail([=](const MTP::Error &error) {
		_updates.fire({ FormError{ error.type() } });
	}).send();
}

void Form::processForm(const MTPDpayments_paymentForm &data) {
	_session->data().processUsers(data.vusers());

	data.vinvoice().match([&](const auto &data) {
		processInvoice(data);
	});
	processDetails(data);
	if (const auto info = data.vsaved_info()) {
		info->match([&](const auto &data) {
			processSavedInformation(data);
		});
	}
	if (const auto credentials = data.vsaved_credentials()) {
		credentials->match([&](const auto &data) {
			processSavedCredentials(data);
		});
	}

	_updates.fire({ FormReady{} });
}

void Form::processInvoice(const MTPDinvoice &data) {
	auto &&prices = ranges::views::all(
		data.vprices().v
	) | ranges::views::transform([](const MTPLabeledPrice &price) {
		return price.match([&](const MTPDlabeledPrice &data) {
			return Ui::LabeledPrice{
				.label = qs(data.vlabel()),
				.price = data.vamount().v,
			};
		});
	});
	_invoice = Ui::Invoice{
		.prices = prices | ranges::to_vector,
		.currency = qs(data.vcurrency()),

		.isNameRequested = data.is_name_requested(),
		.isPhoneRequested = data.is_phone_requested(),
		.isEmailRequested = data.is_email_requested(),
		.isShippingAddressRequested = data.is_shipping_address_requested(),
		.isFlexible = data.is_flexible(),
		.isTest = data.is_test(),

		.phoneSentToProvider = data.is_phone_to_provider(),
		.emailSentToProvider = data.is_email_to_provider(),
	};
}

void Form::processDetails(const MTPDpayments_paymentForm &data) {
	_session->data().processUsers(data.vusers());
	const auto nativeParams = data.vnative_params();
	auto nativeParamsJson = nativeParams
		? nativeParams->match(
			[&](const MTPDdataJSON &data) { return data.vdata().v; })
		: QByteArray();
	_details = FormDetails{
		.url = qs(data.vurl()),
		.nativeProvider = qs(data.vnative_provider().value_or_empty()),
		.nativeParamsJson = std::move(nativeParamsJson),
		.botId = data.vbot_id().v,
		.providerId = data.vprovider_id().v,
		.canSaveCredentials = data.is_can_save_credentials(),
		.passwordMissing = data.is_password_missing(),
	};
}

void Form::processSavedInformation(const MTPDpaymentRequestedInfo &data) {
	const auto address = data.vshipping_address();
	_savedInformation = Ui::SavedInformation{
		.name = qs(data.vname().value_or_empty()),
		.phone = qs(data.vphone().value_or_empty()),
		.email = qs(data.vemail().value_or_empty()),
		.shippingAddress = address ? ParseAddress(*address) : Ui::Address(),
	};
}

void Form::processSavedCredentials(
		const MTPDpaymentSavedCredentialsCard &data) {
	_savedCredentials = Ui::SavedCredentials{
		.id = qs(data.vid()),
		.title = qs(data.vtitle()),
	};
}

void Form::send(const QByteArray &serializedCredentials) {
	_session->api().request(MTPpayments_SendPaymentForm(
		MTP_flags(0),
		MTP_int(_msgId),
		MTPstring(), // requested_info_id
		MTPstring(), // shipping_option_id,
		MTP_inputPaymentCredentials(
			MTP_flags(0),
			MTP_dataJSON(MTP_bytes(serializedCredentials)))
	)).done([=](const MTPpayments_PaymentResult &result) {
		result.match([&](const MTPDpayments_paymentResult &data) {
			_updates.fire({ PaymentFinished{ data.vupdates() } });
		}, [&](const MTPDpayments_paymentVerificationNeeded &data) {
			_updates.fire({ VerificationNeeded{ qs(data.vurl()) } });
		});
	}).fail([=](const MTP::Error &error) {
		_updates.fire({ SendError{ error.type() } });
	}).send();
}

} // namespace Payments
