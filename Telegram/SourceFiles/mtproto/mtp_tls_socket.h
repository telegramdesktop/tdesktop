/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/mtp_abstract_socket.h"

namespace MTP {
namespace internal {

class TlsSocket : public AbstractSocket {
public:
	TlsSocket(
		not_null<QThread*> thread,
		const bytes::vector &secret,
		const ProxyData &proxy);

	void connectToHost(const QString &address, int port) override;
	bool isConnected() override;
	bool hasBytesAvailable() override;
	int64 read(char *buffer, int64 maxLength) override;
	int64 write(const char *buffer, int64 length) override;

	int32 debugState() override;

private:
	enum class State {
		NotConnected,
		Connecting,
		WaitingHello,
		Ready,
		Working,
		Error,
	};

	void plainConnected();
	void plainDisconnected();
	void plainReadyRead();
	void handleError(int errorCode = 0);
	[[nodiscard]] bool requiredHelloPartReady() const;
	void readHello();
	void checkHelloParts12(int parts1Size);
	void checkHelloParts34(int parts123Size);
	void checkHelloDigest();
	void readData();

	QTcpSocket _socket;
	bytes::vector _key;
	State _state = State::NotConnected;
	QByteArray _incoming;
	int16 _serverHelloLength = 0;

};

} // namespace internal
} // namespace MTP
