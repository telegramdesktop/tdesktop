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

	void sendData(mtpBuffer &buffer) override;
	void disconnectFromServer() override;
	void connectTcp(const DcOptions::Endpoint &endpoint) override { // not supported
	}
	void connectHttp(const DcOptions::Endpoint &endpoint) override;
	bool isConnected() const override;
	bool usingHttpWait() override;
	bool needHttpWait() override;

	int32 debugState() const override;

	QString transport() const override;

	static mtpBuffer handleResponse(QNetworkReply *reply);
	static qint32 handleError(QNetworkReply *reply); // returnes error code

public slots:
	void requestFinished(QNetworkReply *reply);

private:
	enum Status {
		WaitingHttp = 0,
		UsingHttp,
		FinishedWork
	};
	Status status;
	MTPint128 httpNonce;
	MTPDdcOption::Flags _flags;

	QNetworkAccessManager manager;
	QUrl address;

	typedef QSet<QNetworkReply*> Requests;
	Requests requests;

};

} // namespace internal
} // namespace MTP
