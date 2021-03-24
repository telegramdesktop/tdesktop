/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "payments/ui/payments_panel_data.h"
#include "mtproto/sender.h"

namespace Main {
class Session;
} // namespace Main

namespace Payments {

struct FormDetails {
	QString url;
	QString nativeProvider;
	QByteArray nativeParamsJson;
	UserId botId = 0;
	UserId providerId = 0;
	bool canSaveCredentials = false;
	bool passwordMissing = false;

	[[nodiscard]] bool valid() const {
		return !url.isEmpty();
	}
	[[nodiscard]] explicit operator bool() const {
		return valid();
	}
};

struct FormReady {};
struct ValidateFinished {};
struct Error {
	enum class Type {
		Form,
		Validate,
		Send,
	};
	Type type = Type::Form;
	QString id;
};
struct VerificationNeeded {
	QString url;
};
struct PaymentFinished {
	MTPUpdates updates;
};

struct FormUpdate {
	std::variant<
		FormReady,
		VerificationNeeded,
		ValidateFinished,
		PaymentFinished,
		Error> data;
};

class Form final {
public:
	Form(not_null<Main::Session*> session, FullMsgId itemId);

	[[nodiscard]] const Ui::Invoice &invoice() const {
		return _invoice;
	}
	[[nodiscard]] const FormDetails &details() const {
		return _details;
	}
	[[nodiscard]] const Ui::RequestedInformation &savedInformation() const {
		return _savedInformation;
	}
	[[nodiscard]] const Ui::SavedCredentials &savedCredentials() const {
		return _savedCredentials;
	}
	[[nodiscard]] const Ui::ShippingOptions &shippingOptions() const {
		return _shippingOptions;
	}

	[[nodiscard]] rpl::producer<FormUpdate> updates() const {
		return _updates.events();
	}

	void validateInformation(const Ui::RequestedInformation &information);
	void setShippingOption(const QString &id);
	void send(const QByteArray &serializedCredentials);

private:
	void requestForm();
	void processForm(const MTPDpayments_paymentForm &data);
	void processInvoice(const MTPDinvoice &data);
	void processDetails(const MTPDpayments_paymentForm &data);
	void processSavedInformation(const MTPDpaymentRequestedInfo &data);
	void processSavedCredentials(
		const MTPDpaymentSavedCredentialsCard &data);
	void processShippingOptions(const QVector<MTPShippingOption> &data);

	const not_null<Main::Session*> _session;
	MTP::Sender _api;
	MsgId _msgId = 0;

	Ui::Invoice _invoice;
	FormDetails _details;
	Ui::RequestedInformation _savedInformation;
	Ui::SavedCredentials _savedCredentials;

	Ui::RequestedInformation _validatedInformation;
	mtpRequestId _validateRequestId = 0;

	Ui::ShippingOptions _shippingOptions;
	QString _requestedInformationId;

	rpl::event_stream<FormUpdate> _updates;

};

} // namespace Payments
