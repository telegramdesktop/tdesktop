/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/connection_tcp.h"

#include "mtproto/details/mtproto_abstract_socket.h"
#include "base/bytes.h"
#include "base/openssl_help.h"
#include "base/qthelp_url.h"

extern "C" {
#include <openssl/aes.h>
} // extern "C"

namespace MTP {
namespace details {
namespace {

constexpr auto kPacketSizeMax = int(0x01000000 * sizeof(mtpPrime));
constexpr auto kFullConnectionTimeout = 8 * crl::time(1000);
constexpr auto kSmallBufferSize = 256 * 1024;
constexpr auto kMinPacketBuffer = 256;
constexpr auto kConnectionStartPrefixSize = 64;

} // namespace

class TcpConnection::Protocol {
public:
	static std::unique_ptr<Protocol> Create(bytes::const_span secret);

	virtual uint32 id() const = 0;
	virtual bool supportsArbitraryLength() const = 0;

	virtual bool requiresExtendedPadding() const = 0;
	virtual void prepareKey(bytes::span key, bytes::const_span source) = 0;
	virtual bytes::span finalizePacket(mtpBuffer &buffer) = 0;

	static constexpr auto kUnknownSize = -1;
	static constexpr auto kInvalidSize = -2;
	virtual int readPacketLength(bytes::const_span bytes) const = 0;
	virtual bytes::const_span readPacket(bytes::const_span bytes) const = 0;

	virtual ~Protocol() = default;

private:
	class Version0;
	class Version1;
	class VersionD;

};

class TcpConnection::Protocol::Version0 : public Protocol {
public:
	uint32 id() const override;
	bool supportsArbitraryLength() const override;

	bool requiresExtendedPadding() const override;
	void prepareKey(bytes::span key, bytes::const_span source) override;
	bytes::span finalizePacket(mtpBuffer &buffer) override;

	int readPacketLength(bytes::const_span bytes) const override;
	bytes::const_span readPacket(bytes::const_span bytes) const override;

};

uint32 TcpConnection::Protocol::Version0::id() const {
	return 0xEFEFEFEFU;
}

bool TcpConnection::Protocol::Version0::supportsArbitraryLength() const {
	return false;
}

bool TcpConnection::Protocol::Version0::requiresExtendedPadding() const {
	return false;
}

void TcpConnection::Protocol::Version0::prepareKey(
		bytes::span key,
		bytes::const_span source) {
	bytes::copy(key, source);
}

bytes::span TcpConnection::Protocol::Version0::finalizePacket(
		mtpBuffer &buffer) {
	Expects(buffer.size() > 2 && buffer.size() < 0x1000003U);

	const auto intsSize = uint32(buffer.size() - 2);
	const auto bytesSize = intsSize * sizeof(mtpPrime);
	const auto data = reinterpret_cast<uchar*>(&buffer[0]);
	const auto added = [&] {
		if (intsSize < 0x7F) {
			data[7] = uchar(intsSize);
			return 1;
		}
		data[4] = uchar(0x7F);
		data[5] = uchar(intsSize & 0xFF);
		data[6] = uchar((intsSize >> 8) & 0xFF);
		data[7] = uchar((intsSize >> 16) & 0xFF);
		return 4;
	}();
	return bytes::make_span(buffer).subspan(8 - added, added + bytesSize);
}

int TcpConnection::Protocol::Version0::readPacketLength(
		bytes::const_span bytes) const {
	if (bytes.empty()) {
		return kUnknownSize;
	}

	const auto first = static_cast<char>(bytes[0]);
	if (first == 0x7F) {
		if (bytes.size() < 4) {
			return kUnknownSize;
		}
		const auto ints = static_cast<uint32>(bytes[1])
			| (static_cast<uint32>(bytes[2]) << 8)
			| (static_cast<uint32>(bytes[3]) << 16);
		return (ints >= 0x7F) ? (int(ints << 2) + 4) : kInvalidSize;
	} else if (first > 0 && first < 0x7F) {
		const auto ints = uint32(first);
		return int(ints << 2) + 1;
	}
	return kInvalidSize;
}

bytes::const_span TcpConnection::Protocol::Version0::readPacket(
		bytes::const_span bytes) const {
	const auto size = readPacketLength(bytes);
	Assert(size != kUnknownSize
		&& size != kInvalidSize
		&& size <= bytes.size());
	const auto sizeLength = (static_cast<char>(bytes[0]) == 0x7F) ? 4 : 1;
	return bytes.subspan(sizeLength, size - sizeLength);
}

class TcpConnection::Protocol::Version1 : public Version0 {
public:
	explicit Version1(bytes::vector &&secret);

	bool requiresExtendedPadding() const override;
	void prepareKey(bytes::span key, bytes::const_span source) override;

private:
	bytes::vector _secret;

};

TcpConnection::Protocol::Version1::Version1(bytes::vector &&secret)
: _secret(std::move(secret)) {
}

bool TcpConnection::Protocol::Version1::requiresExtendedPadding() const {
	return true;
}

void TcpConnection::Protocol::Version1::prepareKey(
		bytes::span key,
		bytes::const_span source) {
	const auto payload = bytes::concatenate(source, _secret);
	bytes::copy(key, openssl::Sha256(payload));
}

class TcpConnection::Protocol::VersionD : public Version1 {
public:
	using Version1::Version1;

	uint32 id() const override;
	bool supportsArbitraryLength() const override;

	bytes::span finalizePacket(mtpBuffer &buffer) override;

	int readPacketLength(bytes::const_span bytes) const override;
	bytes::const_span readPacket(bytes::const_span bytes) const override;

};

uint32 TcpConnection::Protocol::VersionD::id() const {
	return 0xDDDDDDDDU;
}

bool TcpConnection::Protocol::VersionD::supportsArbitraryLength() const {
	return true;
}

bytes::span TcpConnection::Protocol::VersionD::finalizePacket(
		mtpBuffer &buffer) {
	Expects(buffer.size() > 2 && buffer.size() < 0x1000003U);

	const auto intsSize = uint32(buffer.size() - 2);
	const auto padding = openssl::RandomValue<uint32>() & 0x0F;
	const auto bytesSize = intsSize * sizeof(mtpPrime) + padding;
	buffer[1] = bytesSize;
	for (auto added = 0; added < padding; added += 4) {
		buffer.push_back(openssl::RandomValue<mtpPrime>());
	}

	return bytes::make_span(buffer).subspan(4, 4 + bytesSize);
}

int TcpConnection::Protocol::VersionD::readPacketLength(
		bytes::const_span bytes) const {
	if (bytes.size() < 4) {
		return kUnknownSize;
	}
	const auto value = *reinterpret_cast<const uint32*>(bytes.data()) + 4;
	return (value >= 8 && value < kPacketSizeMax)
		? int(value)
		: kInvalidSize;
}

bytes::const_span TcpConnection::Protocol::VersionD::readPacket(
		bytes::const_span bytes) const {
	const auto size = readPacketLength(bytes);
	Assert(size != kUnknownSize
		&& size != kInvalidSize
		&& size <= bytes.size());
	const auto sizeLength = 4;
	return bytes.subspan(sizeLength, size - sizeLength);
}

auto TcpConnection::Protocol::Create(bytes::const_span secret)
-> std::unique_ptr<Protocol> {
	// See also DcOptions::ValidateSecret.
	if ((secret.size() >= 21 && secret[0] == bytes::type(0xEE))
		|| (secret.size() == 17 && secret[0] == bytes::type(0xDD))) {
		return std::make_unique<VersionD>(
			bytes::make_vector(secret.subspan(1, 16)));
	} else if (secret.size() == 16) {
		return std::make_unique<Version1>(bytes::make_vector(secret));
	} else if (secret.empty()) {
		return std::make_unique<Version0>();
	}
	Unexpected("Secret bytes in TcpConnection::Protocol::Create.");
}

TcpConnection::TcpConnection(
	not_null<Instance*> instance,
	QThread *thread,
	const ProxyData &proxy)
: AbstractConnection(thread, proxy)
, _instance(instance)
, _checkNonce(openssl::RandomValue<MTPint128>()) {
}

ConnectionPointer TcpConnection::clone(const ProxyData &proxy) {
	return ConnectionPointer::New<TcpConnection>(_instance, thread(), proxy);
}

void TcpConnection::ensureAvailableInBuffer(int amount) {
	auto &buffer = _usingLargeBuffer ? _largeBuffer : _smallBuffer;
	const auto full = bytes::make_span(buffer).subspan(
		_offsetBytes);
	if (full.size() >= amount) {
		return;
	}
	const auto read = full.subspan(0, _readBytes);
	if (amount <= _smallBuffer.size()) {
		if (_usingLargeBuffer) {
			bytes::copy(_smallBuffer, read);
			_usingLargeBuffer = false;
			_largeBuffer.clear();
		} else {
			bytes::move(_smallBuffer, read);
		}
	} else if (amount <= _largeBuffer.size()) {
		Assert(_usingLargeBuffer);
		bytes::move(_largeBuffer, read);
	} else {
		auto enough = bytes::vector(amount);
		bytes::copy(enough, read);
		_largeBuffer = std::move(enough);
		_usingLargeBuffer = true;
	}
	_offsetBytes = 0;
}

void TcpConnection::socketRead() {
	Expects(_leftBytes > 0 || !_usingLargeBuffer);

	if (!_socket || !_socket->isConnected()) {
		LOG(("MTP Error: Socket not connected in socketRead()"));
		error(kErrorCodeOther);
		return;
	}

	if (_smallBuffer.empty()) {
		_smallBuffer.resize(kSmallBufferSize);
	}
	do {
		const auto readLimit = (_leftBytes > 0)
			? _leftBytes
			: (kSmallBufferSize - _offsetBytes - _readBytes);
		Assert(readLimit > 0);

		auto &buffer = _usingLargeBuffer ? _largeBuffer : _smallBuffer;
		const auto full = bytes::make_span(buffer).subspan(_offsetBytes);
		const auto free = full.subspan(_readBytes);
		const auto readCount = _socket->read(free.subspan(0, readLimit));
		if (readCount > 0) {
			const auto read = free.subspan(0, readCount);
			aesCtrEncrypt(read, _receiveKey, &_receiveState);
			TCP_LOG(("TCP Info: read %1 bytes").arg(readCount));

			_readBytes += readCount;
			if (_leftBytes > 0) {
				Assert(readCount <= _leftBytes);
				_leftBytes -= readCount;
				if (!_leftBytes) {
					socketPacket(full.subspan(0, _readBytes));
					if (!_socket || !_socket->isConnected()) {
						return;
					}

					_usingLargeBuffer = false;
					_largeBuffer.clear();
					_offsetBytes = _readBytes = 0;
				} else {
					TCP_LOG(("TCP Info: not enough %1 for packet! read %2"
						).arg(_leftBytes
						).arg(_readBytes));
					receivedSome();
				}
			} else {
				auto available = full.subspan(0, _readBytes);
				while (_readBytes > 0) {
					const auto packetSize = _protocol->readPacketLength(
						available);
					if (packetSize == Protocol::kUnknownSize) {
						// Not enough bytes yet.
						break;
					} else if (packetSize <= 0) {
						LOG(("TCP Error: bad packet size in 4 bytes: %1"
							).arg(packetSize));
						error(kErrorCodeOther);
						return;
					} else if (available.size() >= packetSize) {
						socketPacket(available.subspan(0, packetSize));
						if (!_socket || !_socket->isConnected()) {
							return;
						}

						available = available.subspan(packetSize);
						_offsetBytes += packetSize;
						_readBytes -= packetSize;

						// If we have too little space left in the buffer.
						ensureAvailableInBuffer(kMinPacketBuffer);
					} else {
						_leftBytes = packetSize - available.size();

						// If the next packet won't fit in the buffer.
						ensureAvailableInBuffer(packetSize);

						TCP_LOG(("TCP Info: not enough %1 for packet! "
							"full size %2 read %3"
							).arg(_leftBytes
							).arg(packetSize
							).arg(available.size()));
						receivedSome();
						break;
					}
				}
			}
		} else if (readCount < 0) {
			LOG(("TCP Error: socket read return %1").arg(readCount));
			error(kErrorCodeOther);
			return;
		} else {
			TCP_LOG(("TCP Info: no bytes read, but bytes available was true..."));
			break;
		}
	} while (_socket
		&& _socket->isConnected()
		&& _socket->hasBytesAvailable());
}

mtpBuffer TcpConnection::parsePacket(bytes::const_span bytes) {
	const auto packet = _protocol->readPacket(bytes);
	TCP_LOG(("TCP Info: packet received, size = %1"
		).arg(packet.size()));
	const auto ints = gsl::make_span(
		reinterpret_cast<const mtpPrime*>(packet.data()),
		packet.size() / sizeof(mtpPrime));
	Assert(!ints.empty());
	if (ints.size() < 3) {
		// nop or error or new quickack, latter is not yet supported.
		if (ints[0] != 0) {
			LOG(("TCP Error: "
				"error packet received, endpoint: '%1:%2', "
				"protocolDcId: %3, code = %4"
				).arg(_address.isEmpty() ? ("prx_" + _proxy.host) : _address
				).arg(_address.isEmpty() ? _proxy.port : _port
				).arg(_protocolDcId
				).arg(ints[0]));
		}
		return mtpBuffer(1, ints[0]);
	}
	auto result = mtpBuffer(ints.size());
	memcpy(result.data(), ints.data(), ints.size() * sizeof(mtpPrime));
	return result;
}

void TcpConnection::socketConnected() {
	Expects(_status == Status::Waiting);

	auto buffer = preparePQFake(_checkNonce);

	DEBUG_LOG(("TCP Info: "
		"dc:%1 - Sending fake req_pq to '%2'"
		).arg(_protocolDcId
		).arg(_address + ':' + QString::number(_port)));

	_pingTime = crl::now();
	sendData(std::move(buffer));
}

void TcpConnection::socketDisconnected() {
	if (_status == Status::Waiting || _status == Status::Ready) {
		disconnected();
	}
}

bool TcpConnection::requiresExtendedPadding() const {
	Expects(_protocol != nullptr);

	return _protocol->requiresExtendedPadding();
}

void TcpConnection::sendData(mtpBuffer &&buffer) {
	Expects(buffer.size() > 2);

	if (!_socket) {
		return;
	}
	char connectionStartPrefixBytes[kConnectionStartPrefixSize];
	const auto connectionStartPrefix = prepareConnectionStartPrefix(
		bytes::make_span(connectionStartPrefixBytes));

	// buffer: 2 available int-s + data + available int.
	const auto bytes = _protocol->finalizePacket(buffer);
	TCP_LOG(("TCP Info: write packet %1 bytes").arg(bytes.size()));
	aesCtrEncrypt(bytes, _sendKey, &_sendState);
	_socket->write(connectionStartPrefix, bytes);
}

bytes::const_span TcpConnection::prepareConnectionStartPrefix(
		bytes::span buffer) {
	Expects(_socket != nullptr);
	Expects(_protocol != nullptr);

	if (_connectionStarted) {
		return {};
	}
	_connectionStarted = true;

	// prepare random part
	char nonceBytes[64];
	const auto nonce = bytes::make_span(nonceBytes);
	do {
		bytes::set_random(nonce);
	} while (!_socket->isGoodStartNonce(nonce));

	// prepare encryption key/iv
	_protocol->prepareKey(
		bytes::make_span(_sendKey),
		nonce.subspan(8, CTRState::KeySize));
	bytes::copy(
		bytes::make_span(_sendState.ivec),
		nonce.subspan(8 + CTRState::KeySize, CTRState::IvecSize));

	// prepare decryption key/iv
	auto reversedBytes = bytes::vector(48);
	const auto reversed = bytes::make_span(reversedBytes);
	bytes::copy(reversed, nonce.subspan(8, reversed.size()));
	std::reverse(reversed.begin(), reversed.end());
	_protocol->prepareKey(
		bytes::make_span(_receiveKey),
		reversed.subspan(0, CTRState::KeySize));
	bytes::copy(
		bytes::make_span(_receiveState.ivec),
		reversed.subspan(CTRState::KeySize, CTRState::IvecSize));

	// write protocol and dc ids
	const auto protocol = reinterpret_cast<uint32*>(nonce.data() + 56);
	*protocol = _protocol->id();
	const auto dcId = reinterpret_cast<int16*>(nonce.data() + 60);
	*dcId = _protocolDcId;

	bytes::copy(buffer, nonce.subspan(0, 56));
	aesCtrEncrypt(nonce, _sendKey, &_sendState);
	bytes::copy(buffer.subspan(56), nonce.subspan(56));

	return buffer;
}

void TcpConnection::disconnectFromServer() {
	if (_status == Status::Finished) {
		return;
	}
	_status = Status::Finished;
	_connectedLifetime.destroy();
	_lifetime.destroy();
	_socket = nullptr;
}

void TcpConnection::connectToServer(
		const QString &address,
		int port,
		const bytes::vector &protocolSecret,
		int16 protocolDcId) {
	Expects(_address.isEmpty());
	Expects(_port == 0);
	Expects(_protocol == nullptr);
	Expects(_protocolDcId == 0);

	const auto secret = (_proxy.type == ProxyData::Type::Mtproto)
		? _proxy.secretFromMtprotoPassword()
		: protocolSecret;
	if (_proxy.type == ProxyData::Type::Mtproto) {
		_address = _proxy.host;
		_port = _proxy.port;
		_protocol = Protocol::Create(secret);

		DEBUG_LOG(("TCP Info: "
			"dc:%1 - Connecting to proxy '%2'"
			).arg(protocolDcId
			).arg(_address + ':' + QString::number(_port)));
	} else {
		_address = address;
		_port = port;
		_protocol = Protocol::Create(secret);

		DEBUG_LOG(("TCP Info: "
			"dc:%1 - Connecting to '%2'"
			).arg(protocolDcId
			).arg(_address + ':' + QString::number(_port)));
	}
	_socket = AbstractSocket::Create(
		thread(),
		secret,
		ToNetworkProxy(_proxy));
	_protocolDcId = protocolDcId;

	_socket->connected(
	) | rpl::start_with_next([=] {
		socketConnected();
	}, _connectedLifetime);

	_socket->disconnected(
	) | rpl::start_with_next([=] {
		socketDisconnected();
	}, _lifetime);

	_socket->readyRead(
	) | rpl::start_with_next([=] {
		socketRead();
	}, _lifetime);

	_socket->error(
	) | rpl::start_with_next([=] {
		socketError();
	}, _lifetime);

	_socket->syncTimeRequests(
	) | rpl::start_with_next([=] {
		syncTimeRequest();
	}, _lifetime);

	_socket->connectToHost(_address, _port);
}

crl::time TcpConnection::pingTime() const {
	return isConnected() ? _pingTime : crl::time(0);
}

crl::time TcpConnection::fullConnectTimeout() const {
	return kFullConnectionTimeout;
}

void TcpConnection::socketPacket(bytes::const_span bytes) {
	Expects(_socket != nullptr);

	// old quickack?..
	const auto data = parsePacket(bytes);
	if (data.size() == 1) {
		if (data[0] != 0) {
			error(data[0]);
		} else {
			// nop
		}
	//} else if (data.size() == 2) {
		// new quickack?..
	} else if (_status == Status::Ready) {
		_receivedQueue.push_back(data);
		receivedData();
	} else if (_status == Status::Waiting) {
		if (const auto res_pq = readPQFakeReply(data)) {
			const auto &data = res_pq->c_resPQ();
			if (data.vnonce() == _checkNonce) {
				DEBUG_LOG(("Connection Info: Valid pq response by TCP."));
				_status = Status::Ready;
				_connectedLifetime.destroy();
				_pingTime = (crl::now() - _pingTime);
				connected();
			} else {
				DEBUG_LOG(("Connection Error: "
					"Wrong nonce received in TCP fake pq-responce"));
				error(kErrorCodeOther);
			}
		} else {
			DEBUG_LOG(("Connection Error: "
				"Could not parse TCP fake pq-responce"));
			error(kErrorCodeOther);
		}
	}
}

void TcpConnection::timedOut() {
	if (_socket) {
		_socket->timedOut();
	}
}

bool TcpConnection::isConnected() const {
	return (_status == Status::Ready);
}

int32 TcpConnection::debugState() const {
	return _socket ? _socket->debugState() : -1;
}

QString TcpConnection::transport() const {
	if (!isConnected()) {
		return QString();
	}
	auto result = qsl("TCP");
	if (qthelp::is_ipv6(_address)) {
		result += qsl("/IPv6");
	}
	return result;
}

QString TcpConnection::tag() const {
	auto result = qsl("TCP");
	if (qthelp::is_ipv6(_address)) {
		result += qsl("/IPv6");
	} else {
		result += qsl("/IPv4");
	}
	return result;
}

void TcpConnection::socketError() {
	if (!_socket) {
		return;
	}

	error(kErrorCodeOther);
}

TcpConnection::~TcpConnection() = default;

} // namespace details
} // namespace MTP
