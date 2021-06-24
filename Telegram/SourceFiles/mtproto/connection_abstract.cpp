/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/connection_abstract.h"

#include "mtproto/connection_tcp.h"
#include "mtproto/connection_http.h"
#include "mtproto/connection_resolving.h"
#include "mtproto/session.h"
#include "base/unixtime.h"
#include "base/openssl_help.h"

namespace MTP {
namespace details {

ConnectionPointer::ConnectionPointer() = default;

ConnectionPointer::ConnectionPointer(std::nullptr_t) {
}

ConnectionPointer::ConnectionPointer(AbstractConnection *value)
: _value(value) {
}

ConnectionPointer::ConnectionPointer(ConnectionPointer &&other)
: _value(base::take(other._value)) {
}

ConnectionPointer &ConnectionPointer::operator=(ConnectionPointer &&other) {
	reset(base::take(other._value));
	return *this;
}

AbstractConnection *ConnectionPointer::get() const {
	return _value;
}

void ConnectionPointer::reset(AbstractConnection *value) {
	if (_value == value) {
		return;
	} else if (const auto old = base::take(_value)) {
		const auto disconnect = [&](auto signal) {
			old->disconnect(old, signal, nullptr, nullptr);
		};
		disconnect(&AbstractConnection::receivedData);
		disconnect(&AbstractConnection::receivedSome);
		disconnect(&AbstractConnection::error);
		disconnect(&AbstractConnection::connected);
		disconnect(&AbstractConnection::disconnected);
		old->disconnectFromServer();
		old->deleteLater();
	}
	_value = value;
}

ConnectionPointer::operator AbstractConnection*() const {
	return get();
}

AbstractConnection *ConnectionPointer::operator->() const {
	return get();
}

AbstractConnection &ConnectionPointer::operator*() const {
	return *get();
}

ConnectionPointer::operator bool() const {
	return get() != nullptr;
}

ConnectionPointer::~ConnectionPointer() {
	reset();
}

mtpBuffer AbstractConnection::prepareSecurePacket(
		uint64 keyId,
		MTPint128 msgKey,
		uint32 size) const {
	auto result = mtpBuffer();
	constexpr auto kTcpPrefixInts = 2;
	constexpr auto kAuthKeyIdPosition = kTcpPrefixInts;
	constexpr auto kAuthKeyIdInts = 2;
	constexpr auto kMessageKeyPosition = kAuthKeyIdPosition
		+ kAuthKeyIdInts;
	constexpr auto kMessageKeyInts = 4;
	constexpr auto kPrefixInts = kTcpPrefixInts
		+ kAuthKeyIdInts
		+ kMessageKeyInts;
	constexpr auto kTcpPostfixInts = 4;
	result.reserve(kPrefixInts + size + kTcpPostfixInts);
	result.resize(kPrefixInts);
	*reinterpret_cast<uint64*>(&result[kAuthKeyIdPosition]) = keyId;
	*reinterpret_cast<MTPint128*>(&result[kMessageKeyPosition]) = msgKey;
	return result;
}

gsl::span<const mtpPrime> AbstractConnection::parseNotSecureResponse(
		const mtpBuffer &buffer) const {
	const auto answer = buffer.data();
	const auto len = buffer.size();
	if (len < 6) {
		LOG(("Not Secure Error: bad request answer, len = %1"
			).arg(len * sizeof(mtpPrime)));
		DEBUG_LOG(("Not Secure Error: answer bytes %1"
			).arg(Logs::mb(answer, len * sizeof(mtpPrime)).str()));
		return {};
	}
	if (answer[0] != 0
		|| answer[1] != 0
		|| (((uint32)answer[2]) & 0x03) != 1
		//|| (base::unixtime::now() - answer[3] > 300) // We didn't sync time yet.
		//|| (answer[3] - base::unixtime::now() > 60)
		|| false) {
		LOG(("Not Secure Error: bad request answer start (%1 %2 %3)"
			).arg(answer[0]
			).arg(answer[1]
			).arg(answer[2]));
		DEBUG_LOG(("Not Secure Error: answer bytes %1"
			).arg(Logs::mb(answer, len * sizeof(mtpPrime)).str()));
		return {};
	}
	const auto answerLen = (uint32)answer[4];
	if (answerLen < 1 || answerLen > (len - 5) * sizeof(mtpPrime)) {
		LOG(("Not Secure Error: bad request answer 1 <= %1 <= %2"
			).arg(answerLen
			).arg((len - 5) * sizeof(mtpPrime)));
		DEBUG_LOG(("Not Secure Error: answer bytes %1"
			).arg(Logs::mb(answer, len * sizeof(mtpPrime)).str()));
		return {};
	}
	return gsl::make_span(answer + 5, answerLen);
}

mtpBuffer AbstractConnection::preparePQFake(const MTPint128 &nonce) const {
	return prepareNotSecurePacket(
		MTPReq_pq(nonce),
		base::unixtime::mtproto_msg_id());
}

std::optional<MTPResPQ> AbstractConnection::readPQFakeReply(
		const mtpBuffer &buffer) const {
	const auto answer = parseNotSecureResponse(buffer);
	if (answer.empty()) {
		return std::nullopt;
	}
	auto from = answer.data();
	MTPResPQ response;
	return response.read(from, from + answer.size())
		? std::make_optional(response)
		: std::nullopt;
}

AbstractConnection::AbstractConnection(
	QThread *thread,
	const ProxyData &proxy)
: _proxy(proxy) {
	moveToThread(thread);
}

ConnectionPointer AbstractConnection::Create(
		not_null<Instance*> instance,
		DcOptions::Variants::Protocol protocol,
		QThread *thread,
		const bytes::vector &secret,
		const ProxyData &proxy) {
	auto result = [&] {
		if (protocol == DcOptions::Variants::Tcp) {
			return ConnectionPointer::New<TcpConnection>(
				instance,
				thread,
				proxy);
		} else {
			return ConnectionPointer::New<HttpConnection>(thread, proxy);
		}
	}();
	if (proxy.tryCustomResolve()) {
		return ConnectionPointer::New<ResolvingConnection>(
			instance,
			thread,
			proxy,
			std::move(result));
	}
	return result;
}

uint32 AbstractConnection::extendedNotSecurePadding() const {
	return uint32(openssl::RandomValue<uchar>() & 0x3F);
}

} // namespace details
} // namespace MTP
