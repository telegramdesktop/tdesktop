/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/bytes.h"
#include "base/basic_types.h"

namespace MTP::details {

class AbstractSocket : protected QObject {
public:
	static std::unique_ptr<AbstractSocket> Create(
		not_null<QThread*> thread,
		const bytes::vector &secret,
		const QNetworkProxy &proxy,
		bool protocolForFiles);

	explicit AbstractSocket(not_null<QThread*> thread) {
		moveToThread(thread);
	}
	virtual ~AbstractSocket() = default;

	[[nodiscard]] rpl::producer<> connected() const {
		return _connected.events();
	}
	[[nodiscard]] rpl::producer<> disconnected() const {
		return _disconnected.events();
	}
	[[nodiscard]] rpl::producer<> readyRead() const {
		return _readyRead.events();
	}
	[[nodiscard]] rpl::producer<> error() const {
		return _error.events();
	}
	[[nodiscard]] rpl::producer<> syncTimeRequests() const {
		return _syncTimeRequests.events();
	}

	virtual void connectToHost(const QString &address, int port) = 0;
	[[nodiscard]] virtual bool isGoodStartNonce(bytes::const_span nonce) = 0;
	virtual void timedOut() = 0;
	[[nodiscard]] virtual bool isConnected() = 0;
	[[nodiscard]] virtual bool hasBytesAvailable() = 0;
	[[nodiscard]] virtual int64 read(bytes::span buffer) = 0;
	virtual void write(
		bytes::const_span prefix,
		bytes::const_span buffer) = 0;

	virtual int32 debugState() = 0;

protected:
	static const int kFilesSendBufferSize = 2 * 1024 * 1024;
	static const int kFilesReceiveBufferSize = 2 * 1024 * 1024;

	rpl::event_stream<> _connected;
	rpl::event_stream<> _disconnected;
	rpl::event_stream<> _readyRead;
	rpl::event_stream<> _error;
	rpl::event_stream<> _syncTimeRequests;

};

} // namespace MTP::details
