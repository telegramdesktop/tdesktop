/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "mtproto/details/mtproto_abstract_socket.h"

namespace MTP::details {

class TcpSocket final : public AbstractSocket {
public:
	TcpSocket(
		not_null<QThread*> thread,
		const QNetworkProxy &proxy,
		bool protocolForFiles);

	void connectToHost(const QString &address, int port) override;
	bool isGoodStartNonce(bytes::const_span nonce) override;
	void timedOut() override;
	bool isConnected() override;
	bool hasBytesAvailable() override;
	int64 read(bytes::span buffer) override;
	void write(bytes::const_span prefix, bytes::const_span buffer) override;

	int32 debugState() override;
	QString debugPostfix() const override;

private:
	void handleError(int errorCode);

	QTcpSocket _socket;

};

} // namespace MTP::details
