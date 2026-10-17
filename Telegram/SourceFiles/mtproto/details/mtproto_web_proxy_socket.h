/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/details/mtproto_abstract_socket.h"
#include "mtproto/mtproto_proxy_data.h"

#include <QtCore/QByteArray>

namespace MTP::WebProxy {
class Transport;
} // namespace MTP::WebProxy

namespace MTP::details {

class WebProxySocket final : public AbstractSocket {
public:
	WebProxySocket(
		not_null<QThread*> thread,
		const ProxyData &proxy);
	~WebProxySocket();

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
	enum class State {
		NotConnected,
		Connecting,
		Connected,
		Disconnected,
		Error,
	};

	void transportConnected();
	void transportData(QByteArray data);
	void transportDisconnected();
	void transportFailed();

	const uint32 _streamId;
	WebProxy::Transport *_transport = nullptr;
	QByteArray _incoming;
	int _incomingOffset = 0;
	State _state = State::NotConnected;

};

} // namespace MTP::details
