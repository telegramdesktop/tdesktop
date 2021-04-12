/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "payments/ui/payments_panel_data.h"
#include "base/weak_ptr.h"
#include "mtproto/sender.h"

class Image;
class QJsonObject;

namespace Core {
struct CloudPasswordResult;
} // namespace Core

namespace Stripe {
class APIClient;
} // namespace Stripe

namespace SmartGlocal {
class APIClient;
} // namespace SmartGlocal

namespace Main {
class Session;
} // namespace Main

namespace Data {
class PhotoMedia;
} // namespace Data

namespace Payments {

enum class Mode;

struct FormDetails {
	uint64 formId = 0;
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

struct ThumbnailLoadProcess {
	std::shared_ptr<Data::PhotoMedia> view;
	bool blurredSet = false;
	rpl::lifetime lifetime;
};

struct SavedCredentials {
	QString id;
	QString title;

	[[nodiscard]] bool valid() const {
		return !id.isEmpty();
	}
	[[nodiscard]] explicit operator bool() const {
		return valid();
	}
};

struct NewCredentials {
	QString title;
	QByteArray data;
	bool saveOnServer = false;

	[[nodiscard]] bool empty() const {
		return data.isEmpty();
	}
	[[nodiscard]] explicit operator bool() const {
		return !empty();
	}
};

struct StripePaymentMethod {
	QString publishableKey;
};

struct SmartGlocalPaymentMethod {
	QString publicToken;
};

struct NativePaymentMethod {
	std::variant<
		v::null_t,
		StripePaymentMethod,
		SmartGlocalPaymentMethod> data;

	[[nodiscard]] bool valid() const {
		return !v::is_null(data);
	}
	[[nodiscard]] explicit operator bool() const {
		return valid();
	}
};

struct PaymentMethod {
	NativePaymentMethod native;
	SavedCredentials savedCredentials;
	NewCredentials newCredentials;
	Ui::PaymentMethodDetails ui;
};

struct ToggleProgress {
	bool shown = true;
};
struct FormReady {};
struct ThumbnailUpdated {
	QImage thumbnail;
};
struct ValidateFinished {};
struct PaymentMethodUpdate {
	bool requestNewPassword = false;
};
struct VerificationNeeded {
	QString url;
};
struct TmpPasswordRequired {};
struct BotTrustRequired {
	not_null<UserData*> bot;
	not_null<UserData*> provider;
};
struct PaymentFinished {
	MTPUpdates updates;
};
struct Error {
	enum class Type {
		None,
		Form,
		Validate,
		Stripe,
		SmartGlocal,
		TmpPassword,
		Send,
	};
	Type type = Type::None;
	QString id;

	[[nodiscard]] bool empty() const {
		return (type == Type::None);
	}
	[[nodiscard]] explicit operator bool() const {
		return !empty();
	}
};

struct FormUpdate : std::variant<
	ToggleProgress,
	FormReady,
	ThumbnailUpdated,
	ValidateFinished,
	PaymentMethodUpdate,
	VerificationNeeded,
	TmpPasswordRequired,
	BotTrustRequired,
	PaymentFinished,
	Error> {
	using variant::variant;
};

class Form final : public base::has_weak_ptr {
public:
	Form(not_null<PeerData*> peer, MsgId itemId, bool receipt);
	~Form();

	[[nodiscard]] const Ui::Invoice &invoice() const {
		return _invoice;
	}
	[[nodiscard]] const FormDetails &details() const {
		return _details;
	}
	[[nodiscard]] const Ui::RequestedInformation &information() const {
		return _information;
	}
	[[nodiscard]] const PaymentMethod &paymentMethod() const {
		return _paymentMethod;
	}
	[[nodiscard]] const Ui::ShippingOptions &shippingOptions() const {
		return _shippingOptions;
	}
	[[nodiscard]] bool hasChanges() const;

	[[nodiscard]] rpl::producer<FormUpdate> updates() const {
		return _updates.events();
	}

	void validateInformation(const Ui::RequestedInformation &information);
	void validateCard(
		const Ui::UncheckedCardDetails &details,
		bool saveInformation);
	void setPaymentCredentials(const NewCredentials &credentials);
	void setHasPassword(bool has);
	void setShippingOption(const QString &id);
	void setTips(int64 value);
	void trustBot();
	void submit();
	void submit(const Core::CloudPasswordResult &result);

private:
	void fillInvoiceFromMessage();
	void showProgress();
	void hideProgress();

	void loadThumbnail(not_null<PhotoData*> photo);
	[[nodiscard]] QImage prepareGoodThumbnail(
		const std::shared_ptr<Data::PhotoMedia> &view) const;
	[[nodiscard]] QImage prepareBlurredThumbnail(
		const std::shared_ptr<Data::PhotoMedia> &view) const;
	[[nodiscard]] QImage prepareThumbnail(
		not_null<const Image*> image,
		bool blurred = false) const;
	[[nodiscard]] QImage prepareEmptyThumbnail() const;

	void requestForm();
	void requestReceipt();
	void processForm(const MTPDpayments_paymentForm &data);
	void processReceipt(const MTPDpayments_paymentReceipt &data);
	void processInvoice(const MTPDinvoice &data);
	void processDetails(const MTPDpayments_paymentForm &data);
	void processDetails(const MTPDpayments_paymentReceipt &data);
	void processSavedInformation(const MTPDpaymentRequestedInfo &data);
	void processSavedCredentials(
		const MTPDpaymentSavedCredentialsCard &data);
	void processShippingOptions(const QVector<MTPShippingOption> &data);
	void fillPaymentMethodInformation();
	void fillStripeNativeMethod(QJsonObject object);
	void fillSmartGlocalNativeMethod(QJsonObject object);
	void refreshPaymentMethodDetails();
	[[nodiscard]] QString defaultPhone() const;
	[[nodiscard]] QString defaultCountry() const;

	void validateCard(
		const StripePaymentMethod &method,
		const Ui::UncheckedCardDetails &details,
		bool saveInformation);
	void validateCard(
		const SmartGlocalPaymentMethod &method,
		const Ui::UncheckedCardDetails &details,
		bool saveInformation);

	bool validateInformationLocal(
		const Ui::RequestedInformation &information) const;
	[[nodiscard]] Error informationErrorLocal(
		const Ui::RequestedInformation &information) const;

	bool validateCardLocal(
		const Ui::UncheckedCardDetails &details) const;
	[[nodiscard]] Error cardErrorLocal(
		const Ui::UncheckedCardDetails &details) const;

	const not_null<Main::Session*> _session;
	MTP::Sender _api;
	not_null<PeerData*> _peer;
	MsgId _msgId = 0;
	bool _receiptMode = false;

	Ui::Invoice _invoice;
	std::unique_ptr<ThumbnailLoadProcess> _thumbnailLoadProcess;
	FormDetails _details;
	Ui::RequestedInformation _savedInformation;
	Ui::RequestedInformation _information;
	PaymentMethod _paymentMethod;

	Ui::RequestedInformation _validatedInformation;
	mtpRequestId _validateRequestId = 0;
	mtpRequestId _passwordRequestId = 0;

	std::unique_ptr<Stripe::APIClient> _stripe;
	std::unique_ptr<SmartGlocal::APIClient> _smartglocal;

	Ui::ShippingOptions _shippingOptions;
	QString _requestedInformationId;

	rpl::event_stream<FormUpdate> _updates;

};

} // namespace Payments
