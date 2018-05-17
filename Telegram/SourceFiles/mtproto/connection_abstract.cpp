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

namespace MTP {
namespace internal {
namespace {

bytes::vector ProtocolSecretFromPassword(const QString &password) {
	const auto size = password.size();
	if (size % 2) {
		return {};
	}
	const auto length = size / 2;
	const auto fromHex = [](QChar ch) -> int {
		const auto code = int(ch.unicode());
		if (code >= '0' && code <= '9') {
			return (code - '0');
		} else if (code >= 'A' && code <= 'F') {
			return 10 + (code - 'A');
		} else if (ch >= 'a' && ch <= 'f') {
			return 10 + (code - 'a');
		}
		return -1;
	};
	auto result = bytes::vector(length);
	for (auto i = 0; i != length; ++i) {
		const auto high = fromHex(password[2 * i]);
		const auto low = fromHex(password[2 * i + 1]);
		if (high < 0 || low < 0) {
			return {};
		}
		result[i] = static_cast<gsl::byte>(high * 16 + low);
	}
	return result;
}

} // namespace

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

AbstractConnection::~AbstractConnection() {
}

mtpBuffer AbstractConnection::preparePQFake(const MTPint128 &nonce) {
	MTPReq_pq req_pq(nonce);
	mtpBuffer buffer;
	uint32 requestSize = req_pq.innerLength() >> 2;

	buffer.resize(0);
	buffer.reserve(8 + requestSize);
	buffer.push_back(0); // tcp packet len
	buffer.push_back(0); // tcp packet num
	buffer.push_back(0);
	buffer.push_back(0);
	buffer.push_back(0);
	buffer.push_back(unixtime());
	buffer.push_back(requestSize * 4);
	req_pq.write(buffer);
	buffer.push_back(0); // tcp crc32 hash

	return buffer;
}

MTPResPQ AbstractConnection::readPQFakeReply(const mtpBuffer &buffer) {
	const mtpPrime *answer(buffer.constData());
	uint32 len = buffer.size();
	if (len < 5) {
		LOG(("Fake PQ Error: bad request answer, len = %1").arg(len * sizeof(mtpPrime)));
		DEBUG_LOG(("Fake PQ Error: answer bytes %1").arg(Logs::mb(answer, len * sizeof(mtpPrime)).str()));
		throw Exception("bad pq reply");
	}
	if (answer[0] != 0 || answer[1] != 0 || (((uint32)answer[2]) & 0x03) != 1/* || (unixtime() - answer[3] > 300) || (answer[3] - unixtime() > 60)*/) { // didnt sync time yet
		LOG(("Fake PQ Error: bad request answer start (%1 %2 %3)").arg(answer[0]).arg(answer[1]).arg(answer[2]));
		DEBUG_LOG(("Fake PQ Error: answer bytes %1").arg(Logs::mb(answer, len * sizeof(mtpPrime)).str()));
		throw Exception("bad pq reply");
	}
	uint32 answerLen = (uint32)answer[4];
	if (answerLen != (len - 5) * sizeof(mtpPrime)) {
		LOG(("Fake PQ Error: bad request answer %1 <> %2").arg(answerLen).arg((len - 5) * sizeof(mtpPrime)));
		DEBUG_LOG(("Fake PQ Error: answer bytes %1").arg(Logs::mb(answer, len * sizeof(mtpPrime)).str()));
		throw Exception("bad pq reply");
	}
	const mtpPrime *from(answer + 5), *end(from + len - 5);
	MTPResPQ response;
	response.read(from, end);
	return response;
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
		const ProxyData &proxy) {
	auto result = [&] {
		if (protocol == DcOptions::Variants::Tcp) {
			return ConnectionPointer::New<TcpConnection>(thread, proxy);
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

} // namespace internal

bytes::vector ProtocolSecretFromPassword(const QString &password) {
	return internal::ProtocolSecretFromPassword(password);
}

} // namespace MTP
