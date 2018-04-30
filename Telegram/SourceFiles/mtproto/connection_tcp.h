/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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

	void setProxyOverride(const ProxyData &proxy) override;
	virtual ~AbstractTCPConnection() = 0;

public slots:
	void socketRead();

protected:
	void writeConnectionStart();

	QTcpSocket sock;
	uint32 packetNum = 0; // sent packet number

	uint32 packetRead = 0;
	uint32 packetLeft = 0; // reading from socket
	bool readingToShort = true;
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
	int16 _protocolDcId = 0;
	bytes::vector _protocolSecret;

};

class TCPConnection : public AbstractTCPConnection {
	Q_OBJECT

public:
	TCPConnection(QThread *thread);

	TimeMs pingTime() const override;
	void sendData(mtpBuffer &buffer) override;
	void disconnectFromServer() override;
	void connectToServer(
		const QString &ip,
		int port,
		const bytes::vector &protocolSecret,
		int16 protocolDcId) override;
	bool isConnected() const override;

	int32 debugState() const override;

	QString transport() const override;
	QString tag() const override;

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

	QString _address;
	int32 _port, _tcpTimeout;
	QTimer tcpTimeoutTimer;
	TimeMs _pingTime = 0;

};

} // namespace internal
} // namespace MTP
