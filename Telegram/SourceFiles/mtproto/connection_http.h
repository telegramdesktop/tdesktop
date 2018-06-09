/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/connection_abstract.h"

namespace MTP {
namespace internal {

class HTTPConnection : public AbstractConnection {
	Q_OBJECT

public:
	HTTPConnection(QThread *thread);

	void setProxyOverride(const ProxyData &proxy) override;
	TimeMs pingTime() const override;
	void sendData(mtpBuffer &buffer) override;
	void disconnectFromServer() override;
	void connectToServer(
		const QString &ip,
		int port,
		const bytes::vector &protocolSecret,
		int16 protocolDcId) override;
	bool isConnected() const override;
	bool usingHttpWait() override;
	bool needHttpWait() override;

	int32 debugState() const override;

	QString transport() const override;
	QString tag() const override;

	static mtpBuffer handleResponse(QNetworkReply *reply);
	static qint32 handleError(QNetworkReply *reply); // returnes error code

public slots:
	void requestFinished(QNetworkReply *reply);

private:
	QUrl url() const;

	enum Status {
		WaitingHttp = 0,
		UsingHttp,
		FinishedWork
	};
	Status status;
	MTPint128 httpNonce;

	QNetworkAccessManager manager;
	QString _address;

	typedef QSet<QNetworkReply*> Requests;
	Requests requests;

	TimeMs _pingTime = 0;

};

} // namespace internal
} // namespace MTP
