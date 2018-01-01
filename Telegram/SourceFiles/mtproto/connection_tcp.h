/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "mtproto/auth_key.h"
#include "mtproto/connection_abstract.h"

namespace MTP {
namespace internal {

class AbstractTCPConnection : public AbstractConnection {
	Q_OBJECT

public:

	AbstractTCPConnection(QThread *thread);
	virtual ~AbstractTCPConnection() = 0;

public slots:

	void socketRead();

protected:

	QTcpSocket sock;
	uint32 packetNum; // sent packet number

	uint32 packetRead, packetLeft; // reading from socket
	bool readingToShort;
	char *currentPos;
	mtpBuffer longBuffer;
	mtpPrime shortBuffer[MTPShortBufferSize];
	virtual void socketPacket(const char *packet, uint32 length) = 0;

	static mtpBuffer handleResponse(const char *packet, uint32 length);
	static void handleError(QAbstractSocket::SocketError e, QTcpSocket &sock);
	static uint32 fourCharsToUInt(char ch1, char ch2, char ch3, char ch4) {
		char ch[4] = { ch1, ch2, ch3, ch4 };
		return *reinterpret_cast<uint32*>(ch);
	}

	void tcpSend(mtpBuffer &buffer);
	uchar _sendKey[CTRState::KeySize];
	CTRState _sendState;
	uchar _receiveKey[CTRState::KeySize];
	CTRState _receiveState;

};

class TCPConnection : public AbstractTCPConnection {
	Q_OBJECT

public:

	TCPConnection(QThread *thread);

	void sendData(mtpBuffer &buffer) override;
	void disconnectFromServer() override;
	void connectTcp(const DcOptions::Endpoint &endpoint) override;
	void connectHttp(const DcOptions::Endpoint &endpoint) override { // not supported
	}
	bool isConnected() const override;

	int32 debugState() const override;

	QString transport() const override;

public slots:

	void socketError(QAbstractSocket::SocketError e);

	void onSocketConnected();
	void onSocketDisconnected();

	void onTcpTimeoutTimer();

protected:

	void socketPacket(const char *packet, uint32 length) override;

private:

	enum Status {
		WaitingTcp = 0,
		UsingTcp,
		FinishedWork
	};
	Status status;
	MTPint128 tcpNonce;

	QString _addr;
	int32 _port, _tcpTimeout;
	MTPDdcOption::Flags _flags;
	QTimer tcpTimeoutTimer;

};

} // namespace internal
} // namespace MTP
