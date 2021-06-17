/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "smartglocal/smartglocal_api_client.h"

#include "smartglocal/smartglocal_error.h"
#include "smartglocal/smartglocal_token.h"

#include <QtCore/QJsonObject>
#include <QtCore/QJsonDocument>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <crl/crl_on_main.h>

namespace SmartGlocal {
namespace {

[[nodiscard]] QString APIURLBase(bool isTest) {
	return isTest
		? "tgb-playground.smart-glocal.com/cds/v1"
		: "tgb.smart-glocal.com/cds/v1";
}

[[nodiscard]] QString TokenEndpoint() {
	return "tokenize/card";
}

[[nodiscard]] QByteArray ToJson(const Stripe::CardParams &card) {
	const auto zero = QChar('0');
	const auto month = QString("%1").arg(card.expMonth, 2, 10, zero);
	const auto year = QString("%1").arg(card.expYear % 100, 2, 10, zero);

	return QJsonDocument(QJsonObject{
		{ "card", QJsonObject{
			{ "number", card.number },
			{ "expiration_month", month },
			{ "expiration_year", year },
			{ "security_code", card.cvc },
		} },
	}).toJson(QJsonDocument::Compact);
}

} // namespace

APIClient::APIClient(PaymentConfiguration configuration)
: _apiUrl("https://" + APIURLBase(configuration.isTest))
, _configuration(configuration) {
	_additionalHttpHeaders = {
		{ "X-PUBLIC-TOKEN", _configuration.publicToken },
	};
}

APIClient::~APIClient() {
	const auto destroy = std::move(_old);
}

void APIClient::createTokenWithCard(
		Stripe::CardParams card,
		TokenCompletionCallback completion) {
	createTokenWithData(ToJson(card), std::move(completion));
}

void APIClient::createTokenWithData(
		QByteArray data,
		TokenCompletionCallback completion) {
	const auto url = QUrl(_apiUrl + '/' + TokenEndpoint());
	auto request = QNetworkRequest(url);
	request.setHeader(
		QNetworkRequest::ContentTypeHeader,
		"application/json");
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
		auto token = Token::DecodedObjectFromAPIResponse(
			document.object().value("data").toObject());
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

} // namespace SmartGlocal
