/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "stripe/stripe_api_client.h"

#include "stripe/stripe_error.h"
#include "stripe/stripe_token.h"
#include "stripe/stripe_form_encoder.h"

#include <QtCore/QJsonObject>
#include <QtCore/QJsonDocument>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <crl/crl_on_main.h>

namespace Stripe {
namespace {

[[nodiscard]] QString APIURLBase() {
	return "api.stripe.com/v1";
}

[[nodiscard]] QString TokenEndpoint() {
	return "tokens";
}

[[nodiscard]] QString StripeAPIVersion() {
	return "2015-10-12";
}

[[nodiscard]] QString SDKVersion() {
	return "9.1.0";
}

[[nodiscard]] QString StripeUserAgentDetails() {
	const auto details = QJsonObject{
		{ "lang", "objective-c" },
		{ "bindings_version", SDKVersion() },
	};
	return QString::fromUtf8(
		QJsonDocument(details).toJson(QJsonDocument::Compact));
}

} // namespace

APIClient::APIClient(PaymentConfiguration configuration)
: _apiUrl("https://" + APIURLBase())
, _configuration(configuration) {
	_additionalHttpHeaders = {
		{ "X-Stripe-User-Agent", StripeUserAgentDetails() },
		{ "Stripe-Version", StripeAPIVersion() },
		{ "Authorization", "Bearer " + _configuration.publishableKey },
	};
}

APIClient::~APIClient() {
	const auto destroy = std::move(_old);
}

void APIClient::createTokenWithCard(
		CardParams card,
		TokenCompletionCallback completion) {
	createTokenWithData(
		FormEncoder::formEncodedDataForObject(MakeEncodable(card)),
		std::move(completion));
}

void APIClient::createTokenWithData(
		QByteArray data,
		TokenCompletionCallback completion) {
	const auto url = QUrl(_apiUrl + '/' + TokenEndpoint());
	auto request = QNetworkRequest(url);
	for (const auto &[name, value] : _additionalHttpHeaders) {
		request.setRawHeader(name.toUtf8(), value.toUtf8());
	}
	destroyReplyDelayed(std::move(_reply));
	_reply.reset(_manager.post(request, data));
	const auto finish = [=](Token token, Error error) {
		crl::on_main([
			completion,
			token = std::move(token),
			error = std::move(error)
		] {
			completion(std::move(token), std::move(error));
		});
	};
	const auto finishWithError = [=](Error error) {
		finish(Token::Empty(), std::move(error));
	};
	const auto finishWithToken = [=](Token token) {
		finish(std::move(token), Error::None());
	};
	QObject::connect(_reply.get(), &QNetworkReply::finished, [=] {
		const auto replyError = int(_reply->error());
		const auto replyErrorString = _reply->errorString();
		const auto bytes = _reply->readAll();
		destroyReplyDelayed(std::move(_reply));

		auto parseError = QJsonParseError();
		const auto document = QJsonDocument::fromJson(bytes, &parseError);
		if (!bytes.isEmpty()) {
			if (parseError.error != QJsonParseError::NoError) {
				const auto code = int(parseError.error);
				finishWithError({
					Error::Code::JsonParse,
					QString("InvalidJson%1").arg(code),
					parseError.errorString(),
				});
				return;
			} else if (!document.isObject()) {
				finishWithError({
					Error::Code::JsonFormat,
					"InvalidJsonRoot",
					"Not an object in JSON reply.",
				});
				return;
			}
			const auto object = document.object();
			if (auto error = Error::DecodedObjectFromResponse(object)) {
				finishWithError(std::move(error));
				return;
			}
		}
		if (replyError != QNetworkReply::NoError) {
			finishWithError({
				Error::Code::Network,
				QString("RequestError%1").arg(replyError),
				replyErrorString,
			});
			return;
		}
		auto token = Token::DecodedObjectFromAPIResponse(document.object());
		if (!token) {
			finishWithError({
				Error::Code::JsonFormat,
				"InvalidTokenJson",
				"Could not parse token.",
			});
		}
		finishWithToken(std::move(token));
	});
}

void APIClient::destroyReplyDelayed(std::unique_ptr<QNetworkReply> reply) {
	if (!reply) {
		return;
	}
	const auto raw = reply.get();
	_old.push_back(std::move(reply));
	QObject::disconnect(raw, &QNetworkReply::finished, nullptr, nullptr);
	raw->deleteLater();
	QObject::connect(raw, &QObject::destroyed, [=] {
		for (auto i = begin(_old); i != end(_old); ++i) {
			if (i->get() == raw) {
				i->release();
				_old.erase(i);
				break;
			}
		}
	});
}

} // namespace Stripe
