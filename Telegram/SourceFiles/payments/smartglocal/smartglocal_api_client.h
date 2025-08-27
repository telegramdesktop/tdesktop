/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "stripe/stripe_card_params.h"
#include "smartglocal/smartglocal_callbacks.h"

#include <QtNetwork/QNetworkAccessManager>
#include <QtCore/QString>
#include <map>
#include <memory>

namespace SmartGlocal {

struct PaymentConfiguration {
	QString publicToken;
	QString tokenizeUrl;
	bool isTest = false;
};

class APIClient final {
public:
	explicit APIClient(PaymentConfiguration configuration);
	~APIClient();

	void createTokenWithCard(
		Stripe::CardParams card,
		TokenCompletionCallback completion);
	void createTokenWithData(
		QByteArray data,
		TokenCompletionCallback completion);

private:
	void destroyReplyDelayed(std::unique_ptr<QNetworkReply> reply);

	QString _apiUrl;
	PaymentConfiguration _configuration;
	std::map<QString, QString> _additionalHttpHeaders;
	QNetworkAccessManager _manager;
	std::unique_ptr<QNetworkReply> _reply;
	std::vector<std::unique_ptr<QNetworkReply>> _old;

};

} // namespace SmartGlocal
