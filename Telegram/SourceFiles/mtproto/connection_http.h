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

class HttpConnection : public AbstractConnection {
public:
	HttpConnection(QThread *thread, const ProxyData &proxy);

	ConnectionPointer clone(const ProxyData &proxy) override;

	TimeMs pingTime() const override;
	TimeMs fullConnectTimeout() const override;
	void sendData(mtpBuffer &&buffer) override;
	void disconnectFromServer() override;
	void connectToServer(
		const QString &address,
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

private:
	QUrl url() const;

	void requestFinished(QNetworkReply *reply);

	enum class Status {
		Waiting = 0,
		Ready,
		Finished,
	};
	Status _status = Status::Waiting;
	MTPint128 _checkNonce;

	QNetworkAccessManager _manager;
	QString _address;

	QSet<QNetworkReply*> _requests;

	TimeMs _pingTime = 0;

};

} // namespace internal
} // namespace MTP
