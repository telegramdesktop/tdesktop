/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "payments/ui/payments_panel_data.h"

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

struct FormError {
	QString type;
};

struct SendError {
	QString type;
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
		FormError,
		SendError,
		VerificationNeeded,
		PaymentFinished> data;
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
	[[nodiscard]] const Ui::SavedInformation &savedInformation() const {
		return _savedInformation;
	}
	[[nodiscard]] const Ui::SavedCredentials &savedCredentials() const {
		return _savedCredentials;
	}

	[[nodiscard]] rpl::producer<FormUpdate> updates() const {
		return _updates.events();
	}

	void send(const QByteArray &serializedCredentials);

private:
	void requestForm();
	void processForm(const MTPDpayments_paymentForm &data);
	void processInvoice(const MTPDinvoice &data);
	void processDetails(const MTPDpayments_paymentForm &data);
	void processSavedInformation(const MTPDpaymentRequestedInfo &data);
	void processSavedCredentials(
		const MTPDpaymentSavedCredentialsCard &data);

	const not_null<Main::Session*> _session;
	MsgId _msgId = 0;

	Ui::Invoice _invoice;
	FormDetails _details;
	Ui::SavedInformation _savedInformation;
	Ui::SavedCredentials _savedCredentials;

	rpl::event_stream<FormUpdate> _updates;

};

} // namespace Payments
