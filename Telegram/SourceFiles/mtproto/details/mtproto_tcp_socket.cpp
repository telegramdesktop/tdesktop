/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/details/mtproto_tcp_socket.h"

#include "base/invoke_queued.h"

namespace MTP::details {

TcpSocket::TcpSocket(
	not_null<QThread*> thread,
	const QNetworkProxy &proxy,
	bool protocolForFiles)
: AbstractSocket(thread) {
	_socket.moveToThread(thread);
	_socket.setProxy(proxy);
	if (protocolForFiles) {
		_socket.setSocketOption(
			QAbstractSocket::SendBufferSizeSocketOption,
			kFilesSendBufferSize);
		_socket.setSocketOption(
			QAbstractSocket::ReceiveBufferSizeSocketOption,
			kFilesReceiveBufferSize);
	}
	const auto wrap = [&](auto handler) {
		return [=](auto &&...args) {
			InvokeQueued(this, [=] { handler(args...); });
		};
	};
	using Error = QAbstractSocket::SocketError;
	connect(
		&_socket,
		&QTcpSocket::connected,
		wrap([=] { _connected.fire({}); }));
	connect(
		&_socket,
		&QTcpSocket::disconnected,
		wrap([=] { _disconnected.fire({}); }));
	connect(
		&_socket,
		&QTcpSocket::readyRead,
		wrap([=] { _readyRead.fire({}); }));
	connect(
		&_socket,
		&QAbstractSocket::errorOccurred,
		wrap([=](Error e) { handleError(e); }));
}

void TcpSocket::connectToHost(const QString &address, int port) {
	_socket.connectToHost(address, port);
}

bool TcpSocket::isGoodStartNonce(bytes::const_span nonce) {
	Expects(nonce.size() >= 2 * sizeof(uint32));

	const auto bytes = nonce.data();
	const auto zero = *reinterpret_cast<const uchar*>(bytes);
	const auto first = *reinterpret_cast<const uint32*>(bytes);
	const auto second = *(reinterpret_cast<const uint32*>(bytes) + 1);
	const auto reserved01 = 0x000000EFU;
	const auto reserved11 = 0x44414548U;
	const auto reserved12 = 0x54534F50U;
	const auto reserved13 = 0x20544547U;
	const auto reserved14 = 0xEEEEEEEEU;
	const auto reserved15 = 0xDDDDDDDDU;
	const auto reserved16 = 0x02010316U;
	const auto reserved21 = 0x00000000U;
	return (zero != reserved01)
		&& (first != reserved11)
		&& (first != reserved12)
		&& (first != reserved13)
		&& (first != reserved14)
		&& (first != reserved15)
		&& (first != reserved16)
		&& (second != reserved21);
}

void TcpSocket::timedOut() {
}

bool TcpSocket::isConnected() {
	return (_socket.state() == QAbstractSocket::ConnectedState);
}

bool TcpSocket::hasBytesAvailable() {
	return _socket.bytesAvailable() > 0;
}

int64 TcpSocket::read(bytes::span buffer) {
	return _socket.read(
		reinterpret_cast<char*>(buffer.data()),
		buffer.size());
}

void TcpSocket::write(bytes::const_span prefix, bytes::const_span buffer) {
	Expects(!buffer.empty());

	if (!prefix.empty()) {
		_socket.write(
			reinterpret_cast<const char*>(prefix.data()),
			prefix.size());
	}
	_socket.write(
		reinterpret_cast<const char*>(buffer.data()),
		buffer.size());
}

int32 TcpSocket::debugState() {
	return _socket.state();
}

QString TcpSocket::debugPostfix() const {
	return QString();
}

void TcpSocket::handleError(int errorCode) {
	logError(errorCode, _socket.errorString());
	_error.fire({});
}

} // namespace MTP::details
