/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/details/mtproto_domain_resolver.h"
#include "base/bytes.h"
#include "base/weak_ptr.h"

#include <QtCore/QPointer>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkAccessManager>

namespace MTP::details {

class SpecialConfigRequest : public QObject {
public:
	SpecialConfigRequest(
		Fn<void(
			DcId dcId,
			const std::string &ip,
			int port,
			bytes::const_span secret)> callback,
		const QString &domainString,
		const QString &phone);
	SpecialConfigRequest(
		Fn<void()> timeDoneCallback,
		const QString &domainString);

private:
	enum class Type {
		Mozilla,
		Google,
		RemoteConfig,
		Realtime,
		FireStore,
	};
	struct Attempt {
		Type type;
		QString data;
		QString host;
	};

	SpecialConfigRequest(
		Fn<void(
			DcId dcId,
			const std::string &ip,
			int port,
			bytes::const_span secret)> callback,
		Fn<void()> timeDoneCallback,
		const QString &domainString,
		const QString &phone);

	void sendNextRequest();
	void performRequest(const Attempt &attempt);
	void requestFinished(Type type, not_null<QNetworkReply*> reply);
	void handleHeaderUnixtime(not_null<QNetworkReply*> reply);
	QByteArray finalizeRequest(not_null<QNetworkReply*> reply);
	void handleResponse(const QByteArray &bytes);
	bool decryptSimpleConfig(const QByteArray &bytes);

	Fn<void(
		DcId dcId,
		const std::string &ip,
		int port,
		bytes::const_span secret)> _callback;
	Fn<void()> _timeDoneCallback;
	QString _domainString;
	QString _phone;
	MTPhelp_ConfigSimple _simpleConfig;

	QNetworkAccessManager _manager;
	std::vector<Attempt> _attempts;
	std::vector<ServiceWebRequest> _requests;

};

} // namespace MTP::details
