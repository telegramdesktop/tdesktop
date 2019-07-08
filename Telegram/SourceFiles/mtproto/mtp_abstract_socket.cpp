/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/mtp_abstract_socket.h"

#include "mtproto/mtp_tcp_socket.h"
#include "mtproto/mtp_tls_socket.h"

namespace MTP {
namespace internal {

std::unique_ptr<AbstractSocket> AbstractSocket::Create(
		not_null<QThread*> thread,
		const bytes::vector &secret,
		const ProxyData &proxy) {
	const auto proxySecret = (proxy.type == ProxyData::Type::Mtproto)
		? proxy.secretFromMtprotoPassword()
		: bytes::vector();
	const auto &usingSecret = proxySecret.empty() ? secret : proxySecret;
	if (!usingSecret.empty() && usingSecret[0] == bytes::type(0xEE)) {
		return std::make_unique<TlsSocket>(thread, secret, proxy);
	} else {
		return std::make_unique<TcpSocket>(thread, proxy);
	}
}

} // namespace internal
} // namespace MTP
