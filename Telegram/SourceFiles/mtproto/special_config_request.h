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

	~SpecialConfigRequest();

private:
	void performAppRequest();
	void performDnsRequest();
	void appFinished();
	void dnsFinished();
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
	std::unique_ptr<QNetworkReply> _appReply;
	std::unique_ptr<QNetworkReply> _dnsReply;

	std::unique_ptr<DcOptions> _localOptions;
	std::unique_ptr<Instance> _localInstance;

};

} // namespace MTP
