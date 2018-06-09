/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/bytes.h"

namespace MTP {

class SpecialConfigRequest : public QObject {
public:
	SpecialConfigRequest(
		base::lambda<void(
			DcId dcId,
			const std::string &ip,
			int port,
			bytes::const_span secret)> callback,
		const QString &phone);

private:
	enum class Type {
		App,
		Dns,
	};
	struct Attempt {
		Type type;
		QString domain;
	};
	struct Request {
		Request(not_null<QNetworkReply*> reply);
		Request(Request &&other);
		Request &operator=(Request &&other);
		~Request();

		void destroy();

		QPointer<QNetworkReply> reply;

	};

	void sendNextRequest();
	void performRequest(const Attempt &attempt);
	void requestFinished(Type type, not_null<QNetworkReply*> reply);
	QByteArray finalizeRequest(not_null<QNetworkReply*> reply);
	void handleResponse(const QByteArray &bytes);
	bool decryptSimpleConfig(const QByteArray &bytes);

	base::lambda<void(
		DcId dcId,
		const std::string &ip,
		int port,
		bytes::const_span secret)> _callback;
	QString _phone;
	MTPhelp_ConfigSimple _simpleConfig;

	QNetworkAccessManager _manager;
	std::vector<Attempt> _attempts;
	std::vector<Request> _requests;

};

} // namespace MTP
