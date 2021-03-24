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
			.postcode = qs(data.vpost_code()),
		};
	});
}

[[nodiscard]] std::vector<Ui::LabeledPrice> ParsePrices(
		const MTPVector<MTPLabeledPrice> &data) {
	return ranges::views::all(
		data.v
	) | ranges::views::transform([](const MTPLabeledPrice &price) {
		return price.match([&](const MTPDlabeledPrice &data) {
			return Ui::LabeledPrice{
				.label = qs(data.vlabel()),
				.price = *reinterpret_cast<const int64*>(&data.vamount().v),
			};
		});
	}) | ranges::to_vector;
}

[[nodiscard]] MTPPaymentRequestedInfo Serialize(
		const Ui::RequestedInformation &information) {
	using Flag = MTPDpaymentRequestedInfo::Flag;
	return MTP_paymentRequestedInfo(
		MTP_flags((information.name.isEmpty() ? Flag(0) : Flag::f_name)
			| (information.email.isEmpty() ? Flag(0) : Flag::f_email)
			| (information.phone.isEmpty() ? Flag(0) : Flag::f_phone)
			| (information.shippingAddress
				? Flag::f_shipping_address
				: Flag(0))),
		MTP_string(information.name),
		MTP_string(information.phone),
		MTP_string(information.email),
		MTP_postAddress(
			MTP_string(information.shippingAddress.address1),
			MTP_string(information.shippingAddress.address2),
			MTP_string(information.shippingAddress.city),
			MTP_string(information.shippingAddress.state),
			MTP_string(information.shippingAddress.countryIso2),
			MTP_string(information.shippingAddress.postcode)));
}

} // namespace

Form::Form(not_null<Main::Session*> session, FullMsgId itemId)
: _session(session)
, _api(&_session->mtp())
, _msgId(itemId.msg) {
	requestForm();
}

void Form::requestForm() {
	_api.request(MTPpayments_GetPaymentForm(
		MTP_int(_msgId)
	)).done([=](const MTPpayments_PaymentForm &result) {
		result.match([&](const auto &data) {
			processForm(data);
		});
	}).fail([=](const MTP::Error &error) {
		_updates.fire({ Error{ Error::Type::Form, error.type() } });
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
	_invoice = Ui::Invoice{
		.prices = ParsePrices(data.vprices()),
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
	_savedInformation = Ui::RequestedInformation{
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
	using Flag = MTPpayments_SendPaymentForm::Flag;
	_api.request(MTPpayments_SendPaymentForm(
		MTP_flags((_requestedInformationId.isEmpty()
			? Flag(0)
			: Flag::f_requested_info_id)
			| (_shippingOptions.selectedId.isEmpty()
				? Flag(0)
				: Flag::f_shipping_option_id)),
		MTP_int(_msgId),
		MTP_string(_requestedInformationId),
		MTP_string(_shippingOptions.selectedId),
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
		_updates.fire({ Error{ Error::Type::Send, error.type() } });
	}).send();
}

void Form::validateInformation(const Ui::RequestedInformation &information) {
	if (_validateRequestId) {
		if (_validatedInformation == information) {
			return;
		}
		_api.request(base::take(_validateRequestId)).cancel();
	}
	_validatedInformation = information;
	_validateRequestId = _api.request(MTPpayments_ValidateRequestedInfo(
		MTP_flags(0), // #TODO payments save information
		MTP_int(_msgId),
		Serialize(information)
	)).done([=](const MTPpayments_ValidatedRequestedInfo &result) {
		_validateRequestId = 0;
		const auto oldSelectedId = _shippingOptions.selectedId;
		result.match([&](const MTPDpayments_validatedRequestedInfo &data) {
			_requestedInformationId = data.vid().value_or_empty();
			processShippingOptions(
				data.vshipping_options().value_or_empty());
		});
		_shippingOptions.selectedId = ranges::contains(
			_shippingOptions.list,
			oldSelectedId,
			&Ui::ShippingOption::id
		) ? oldSelectedId : QString();
		if (_shippingOptions.selectedId.isEmpty()
			&& _shippingOptions.list.size() == 1) {
			_shippingOptions.selectedId = _shippingOptions.list.front().id;
		}
		_savedInformation = _validatedInformation;
		_updates.fire({ ValidateFinished{} });
	}).fail([=](const MTP::Error &error) {
		_validateRequestId = 0;
		_updates.fire({ Error{ Error::Type::Validate, error.type() } });
	}).send();
}

void Form::setShippingOption(const QString &id) {
	_shippingOptions.selectedId = id;
}

void Form::processShippingOptions(const QVector<MTPShippingOption> &data) {
	_shippingOptions = Ui::ShippingOptions{ ranges::views::all(
		data
	) | ranges::views::transform([](const MTPShippingOption &option) {
		return option.match([](const MTPDshippingOption &data) {
			return Ui::ShippingOption{
				.id = qs(data.vid()),
				.title = qs(data.vtitle()),
				.prices = ParsePrices(data.vprices()),
			};
		});
	}) | ranges::to_vector };
}

} // namespace Payments
