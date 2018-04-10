/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace MTP {

class SpecialConfigRequest : public QObject {
public:
	SpecialConfigRequest(
		base::lambda<void(
			DcId dcId,
			const std::string &ip,
			int port)> callback);

	~SpecialConfigRequest();

private:
	void performApp1Request();
	void performApp2Request();
	void performDnsRequest();
	void app1Finished();
	void app2Finished();
	void dnsFinished();
	void handleResponse(const QByteArray &bytes);
	bool decryptSimpleConfig(const QByteArray &bytes);

	base::lambda<void(
		DcId dcId,
		const std::string &ip,
		int port)> _callback;
	MTPhelp_ConfigSimple _simpleConfig;

	QNetworkAccessManager _manager;
	std::unique_ptr<QNetworkReply> _app1Reply;
	std::unique_ptr<QNetworkReply> _app2Reply;
	std::unique_ptr<QNetworkReply> _dnsReply;

	std::unique_ptr<DcOptions> _localOptions;
	std::unique_ptr<Instance> _localInstance;

};

} // namespace MTP
