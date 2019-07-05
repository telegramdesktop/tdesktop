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

class TcpSocket : public AbstractSocket {
public:
	TcpSocket(not_null<QThread*> thread, const ProxyData &proxy);
	~TcpSocket();

	void connectToHost(const QString &address, int port) override;
	bool isConnected() override;
	int bytesAvailable() override;
	int64 read(char *buffer, int64 maxLength) override;
	int64 write(const char *buffer, int64 length) override;

	int32 debugState() override;

	static void LogError(int errorCode, const QString &errorText);

private:
	void logError(int errorCode);

	QTcpSocket _socket;

};

} // namespace internal
} // namespace MTP
