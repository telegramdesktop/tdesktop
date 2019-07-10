/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/mtp_tcp_socket.h"

#include "base/invoke_queued.h"

namespace MTP {
namespace internal {

TcpSocket::TcpSocket(not_null<QThread*> thread, const QNetworkProxy &proxy)
: AbstractSocket(thread) {
	_socket.moveToThread(thread);
	_socket.setProxy(proxy);
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

	using ErrorSignal = void(QTcpSocket::*)(QAbstractSocket::SocketError);
	const auto QTcpSocket_error = ErrorSignal(&QAbstractSocket::error);
	connect(
		&_socket,
		QTcpSocket_error,
		wrap([=](Error e) { handleError(e); }));
}

void TcpSocket::connectToHost(const QString &address, int port) {
	_socket.connectToHost(address, port);
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

void TcpSocket::LogError(int errorCode, const QString &errorText) {
	switch (errorCode) {
	case QAbstractSocket::ConnectionRefusedError:
		LOG(("TCP Error: socket connection refused - %1").arg(errorText));
		break;

	case QAbstractSocket::RemoteHostClosedError:
		TCP_LOG(("TCP Info: remote host closed socket connection - %1"
			).arg(errorText));
		break;

	case QAbstractSocket::HostNotFoundError:
		LOG(("TCP Error: host not found - %1").arg(errorText));
		break;

	case QAbstractSocket::SocketTimeoutError:
		LOG(("TCP Error: socket timeout - %1").arg(errorText));
		break;

	case QAbstractSocket::NetworkError:
		LOG(("TCP Error: network - %1").arg(errorText));
		break;

	case QAbstractSocket::ProxyAuthenticationRequiredError:
	case QAbstractSocket::ProxyConnectionRefusedError:
	case QAbstractSocket::ProxyConnectionClosedError:
	case QAbstractSocket::ProxyConnectionTimeoutError:
	case QAbstractSocket::ProxyNotFoundError:
	case QAbstractSocket::ProxyProtocolError:
		LOG(("TCP Error: proxy (%1) - %2").arg(errorCode).arg(errorText));
		break;

	default:
		LOG(("TCP Error: other (%1) - %2").arg(errorCode).arg(errorText));
		break;
	}

	TCP_LOG(("TCP Error %1, restarting! - %2"
		).arg(errorCode
		).arg(errorText));
}

void TcpSocket::handleError(int errorCode) {
	LogError(errorCode, _socket.errorString());
	_error.fire({});
}

} // namespace internal
} // namespace MTP
