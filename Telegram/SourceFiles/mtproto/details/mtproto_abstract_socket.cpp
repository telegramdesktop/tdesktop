/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/details/mtproto_abstract_socket.h"

#include "mtproto/details/mtproto_tcp_socket.h"
#include "mtproto/details/mtproto_tls_socket.h"

namespace MTP::details {

std::unique_ptr<AbstractSocket> AbstractSocket::Create(
		not_null<QThread*> thread,
		const bytes::vector &secret,
		const QNetworkProxy &proxy,
		bool protocolForFiles) {
	if (secret.size() >= 21 && secret[0] == bytes::type(0xEE)) {
		return std::make_unique<TlsSocket>(
			thread,
			secret,
			proxy,
			protocolForFiles);
	} else {
		return std::make_unique<TcpSocket>(thread, proxy, protocolForFiles);
	}
}

void AbstractSocket::logError(int errorCode, const QString &errorText) {
	const auto log = [&](const QString &message) {
		DEBUG_LOG(("Socket %1 Error: ").arg(_debugId) + message);
	};
	switch (errorCode) {
	case QAbstractSocket::ConnectionRefusedError:
		log(u"Socket connection refused - %1."_q.arg(errorText));
		break;

	case QAbstractSocket::RemoteHostClosedError:
		log(u"Remote host closed socket connection - %1."_q.arg(errorText));
		break;

	case QAbstractSocket::HostNotFoundError:
		log(u"Host not found - %1."_q.arg(errorText));
		break;

	case QAbstractSocket::SocketTimeoutError:
		log(u"Socket timeout - %1."_q.arg(errorText));
		break;

	case QAbstractSocket::NetworkError: {
		log(u"Network - %1."_q.arg(errorText));
	} break;

	case QAbstractSocket::ProxyAuthenticationRequiredError:
	case QAbstractSocket::ProxyConnectionRefusedError:
	case QAbstractSocket::ProxyConnectionClosedError:
	case QAbstractSocket::ProxyConnectionTimeoutError:
	case QAbstractSocket::ProxyNotFoundError:
	case QAbstractSocket::ProxyProtocolError:
		log(u"Proxy (%1) - %2."_q.arg(errorCode).arg(errorText));
		break;

	default:
		log(u"Other (%1) - %2."_q.arg(errorCode).arg(errorText));
		break;
	}
}

} // namespace MTP::details
