/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/connection_tcp.h"

namespace MTP {
namespace internal {

class AutoConnection : public AbstractTCPConnection {
	Q_OBJECT

public:

	AutoConnection(QThread *thread);

	void sendData(mtpBuffer &buffer) override;
	void disconnectFromServer() override;
	void connectTcp(const DcOptions::Endpoint &endpoint) override;
	void connectHttp(const DcOptions::Endpoint &endpoint) override;
	bool isConnected() const override;
	bool usingHttpWait() override;
	bool needHttpWait() override;

	int32 debugState() const override;

	QString transport() const override;

public slots:

	void socketError(QAbstractSocket::SocketError e);
	void requestFinished(QNetworkReply *reply);

	void onSocketConnected();
	void onSocketDisconnected();
	void onHttpStart();

	void onTcpTimeoutTimer();

protected:

	void socketPacket(const char *packet, uint32 length) override;

private:

	void httpSend(mtpBuffer &buffer);
	enum Status {
		WaitingBoth = 0,
		WaitingHttp,
		WaitingTcp,
		HttpReady,
		UsingHttp,
		UsingTcp,
		FinishedWork
	};
	Status status;
	MTPint128 tcpNonce, httpNonce;
	QTimer httpStartTimer;

	QNetworkAccessManager manager;
	QUrl address;

	typedef QSet<QNetworkReply*> Requests;
	Requests requests;

	QString _addrTcp, _addrHttp;
	int32 _portTcp, _portHttp;
	MTPDdcOption::Flags _flagsTcp, _flagsHttp;
	int32 _tcpTimeout;
	QTimer tcpTimeoutTimer;

};

} // namespace internal
} // namespace MTP
