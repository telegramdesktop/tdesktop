/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/mtp_abstract_socket.h"

#include "mtproto/mtp_tcp_socket.h"

namespace MTP {
namespace internal {

std::unique_ptr<AbstractSocket> AbstractSocket::Create(
		not_null<QThread*> thread,
		const bytes::vector &secret,
		const ProxyData &proxy) {
	return std::make_unique<TcpSocket>(thread, proxy);
}

} // namespace internal
} // namespace MTP
