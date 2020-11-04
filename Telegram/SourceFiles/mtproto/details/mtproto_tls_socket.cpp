/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/details/mtproto_tls_socket.h"

#include "mtproto/details/mtproto_tcp_socket.h"
#include "base/openssl_help.h"
#include "base/bytes.h"
#include "base/invoke_queued.h"
#include "base/unixtime.h"
#include "base/qt_adapters.h"

#include <QtCore/QtEndian>
#include <range/v3/algorithm/reverse.hpp>

namespace MTP::details {
namespace {

constexpr auto kMaxGrease = 8;
constexpr auto kClientHelloLength = 517;
constexpr auto kHelloDigestLength = 32;
constexpr auto kLengthSize = sizeof(uint16);
const auto kServerHelloPart1 = qstr("\x16\x03\x03");
const auto kServerHelloPart3 = qstr("\x14\x03\x03\x00\x01\x01\x17\x03\x03");
constexpr auto kServerHelloDigestPosition = 11;
const auto kServerHeader = qstr("\x17\x03\x03");
constexpr auto kClientPartSize = 2878;
const auto kClientPrefix = qstr("\x14\x03\x03\x00\x01\x01");
const auto kClientHeader = qstr("\x17\x03\x03");

using BigNum = openssl::BigNum;
using BigNumContext = openssl::Context;

[[nodiscard]] MTPTlsClientHello PrepareClientHelloRules() {
	auto stack = std::vector<QVector<MTPTlsBlock>>();
	const auto pushToBack = [&](MTPTlsBlock &&block) {
		Expects(!stack.empty());

		stack.back().push_back(std::move(block));
	};
	const auto S = [&](QLatin1String s) {
		const auto data = QByteArray(s.data(), s.size());
		pushToBack(MTP_tlsBlockString(MTP_bytes(data)));
	};
	const auto Z = [&](int length) {
		pushToBack(MTP_tlsBlockZero(MTP_int(length)));
	};
	const auto G = [&](int seed) {
		pushToBack(MTP_tlsBlockGrease(MTP_int(seed)));
	};
	const auto R = [&](int length) {
		pushToBack(MTP_tlsBlockRandom(MTP_int(length)));
	};
	const auto D = [&] {
		pushToBack(MTP_tlsBlockDomain());
	};
	const auto K = [&] {
		pushToBack(MTP_tlsBlockPublicKey());
	};
	const auto Open = [&] {
		stack.emplace_back();
	};
	const auto Close = [&] {
		Expects(stack.size() > 1);

		const auto blocks = std::move(stack.back());
		stack.pop_back();
		pushToBack(MTP_tlsBlockScope(MTP_vector<MTPTlsBlock>(blocks)));
	};
	const auto Finish = [&] {
		Expects(stack.size() == 1);

		return stack.back();
	};

	stack.emplace_back();

	S(qstr("\x16\x03\x01\x02\x00\x01\x00\x01\xfc\x03\x03"));
	Z(32);
	S(qstr("\x20"));
	R(32);
	S(qstr("\x00\x20"));
	G(0);
	S(qstr(""
		"\x13\x01\x13\x02\x13\x03\xc0\x2b\xc0\x2f\xc0\x2c\xc0\x30\xcc\xa9"
		"\xcc\xa8\xc0\x13\xc0\x14\x00\x9c\x00\x9d\x00\x2f\x00\x35\x01\x00"
		"\x01\x93"));
	G(2);
	S(qstr("\x00\x00\x00\x00"));
	Open();
	Open();
	S(qstr("\x00"));
	Open();
	D();
	Close();
	Close();
	Close();
	S(qstr("\x00\x17\x00\x00\xff\x01\x00\x01\x00\x00\x0a\x00\x0a\x00\x08"));
	G(4);
	S(qstr(""
		"\x00\x1d\x00\x17\x00\x18\x00\x0b\x00\x02\x01\x00\x00\x23\x00\x00"
		"\x00\x10\x00\x0e\x00\x0c\x02\x68\x32\x08\x68\x74\x74\x70\x2f\x31"
		"\x2e\x31\x00\x05\x00\x05\x01\x00\x00\x00\x00\x00\x0d\x00\x12\x00"
		"\x10\x04\x03\x08\x04\x04\x01\x05\x03\x08\x05\x05\x01\x08\x06\x06"
		"\x01\x00\x12\x00\x00\x00\x33\x00\x2b\x00\x29"));
	G(4);
	S(qstr("\x00\x01\x00\x00\x1d\x00\x20"));
	K();
	S(qstr("\x00\x2d\x00\x02\x01\x01\x00\x2b\x00\x0b\x0a"));
	G(6);
	S(qstr("\x03\x04\x03\x03\x03\x02\x03\x01\x00\x1b\x00\x03\x02\x00\x02"));
	G(3);
	S(qstr("\x00\x01\x00\x00\x15"));

	return MTP_tlsClientHello(MTP_vector<MTPTlsBlock>(Finish()));
}

[[nodiscard]] bytes::vector PrepareGreases() {
	auto result = bytes::vector(kMaxGrease);
	bytes::set_random(result);
	for (auto &byte : result) {
		byte = bytes::type((uchar(byte) & 0xF0) + 0x0A);
	}
	static_assert(kMaxGrease % 2 == 0);
	for (auto i = 0; i != kMaxGrease; i += 2) {
		if (result[i] == result[i + 1]) {
			result[i + 1] = bytes::type(uchar(result[i + 1]) ^ 0x10);
		}
	}
	return result;
}

// Returns y^2 = x^3 + 486662 * x^2 + x.
[[nodiscard]] BigNum GenerateY2(
		const BigNum &x,
		const BigNum &mod,
		const BigNumContext &context) {
	auto coef = BigNum(486662);
	auto y = BigNum::ModAdd(x, coef, mod, context);
	y.setModMul(y, x, mod, context);
	coef.setWord(1);
	y.setModAdd(y, coef, mod, context);
	return BigNum::ModMul(y, x, mod, context);
}

// Returns x_2 = (x^2 - 1)^2/(4*y^2).
[[nodiscard]] BigNum GenerateX2(
		const BigNum &x,
		const BigNum &mod,
		const BigNumContext &context) {
	auto denominator = GenerateY2(x, mod, context);
	auto coef = BigNum(4);
	denominator.setModMul(denominator, coef, mod, context);

	auto numerator = BigNum::ModMul(x, x, mod, context);
	coef.setWord(1);
	numerator.setModSub(numerator, coef, mod, context);
	numerator.setModMul(numerator, numerator, mod, context);

	denominator.setModInverse(denominator, mod, context);
	return BigNum::ModMul(numerator, denominator, mod, context);
}

[[nodiscard]] bytes::vector GeneratePublicKey() {
	const auto context = BigNumContext();
	const char modBytes[] = ""
		"\x7f\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xed";
	const char powBytes[] = ""
		"\x3f\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xf6";
	const auto mod = BigNum(bytes::make_span(modBytes).subspan(0, 32));
	const auto pow = BigNum(bytes::make_span(powBytes).subspan(0, 32));

	auto x = BigNum();
	do {
		while (true) {
			auto random = bytes::vector(32);
			bytes::set_random(random);
			random[31] &= bytes::type(0x7FU);
			x.setBytes(random);
			x.setModMul(x, x, mod, context);

			auto y = GenerateY2(x, mod, context);
			if (BigNum::ModExp(y, pow, mod, context).isOne()) {
				break;
			}
		}
		for (auto i = 0; i != 3; ++i) {
			x = GenerateX2(x, mod, context);
		}
		const auto xBytes = x.getBytes();
		Assert(!xBytes.empty());
		Assert(xBytes.size() <= 32);
	} while (x.bytesSize() == 32);

	const auto xBytes = x.getBytes();
	auto result = bytes::vector(32, bytes::type());
	bytes::copy(
		bytes::make_span(result).subspan(32 - xBytes.size()),
		xBytes);
	ranges::reverse(result);

	//auto string = QString();
	//string.reserve(64);
	//for (const auto byte : result) {
	//	const auto code = uchar(byte);
	//	const auto hex = [](uchar value) -> char {
	//		if (value >= 0 && value <= 9) {
	//			return '0' + value;
	//		} else if (value >= 10 && value <= 15) {
	//			return 'a' + (value - 10);
	//		}
	//		return '-';
	//	};
	//	string.append(hex(code / 16)).append(hex(code % 16));
	//}
	//LOG(("KEY: %1").arg(string));

	return result;
}

struct ClientHello {
	QByteArray data;
	QByteArray digest;
};

class ClientHelloGenerator {
public:
	ClientHelloGenerator(
		const MTPTlsClientHello &rules,
		bytes::const_span domain,
		bytes::const_span key);
	[[nodiscard]] ClientHello result();

private:
	[[nodiscard]] bytes::span grow(int size);
	void writeBlocks(const QVector<MTPTlsBlock> &blocks);
	void writeBlock(const MTPTlsBlock &data);
	void writeBlock(const MTPDtlsBlockString &data);
	void writeBlock(const MTPDtlsBlockZero &data);
	void writeBlock(const MTPDtlsBlockGrease &data);
	void writeBlock(const MTPDtlsBlockRandom &data);
	void writeBlock(const MTPDtlsBlockDomain &data);
	void writeBlock(const MTPDtlsBlockPublicKey &data);
	void writeBlock(const MTPDtlsBlockScope &data);
	void writePadding();
	void writeDigest();
	void writeTimestamp();

	bytes::const_span _domain;
	bytes::const_span _key;
	bytes::vector _greases;
	std::vector<int> _scopeStack;
	QByteArray _result;
	QByteArray _digest;
	int _digestPosition = -1;
	bool _error = false;

};

ClientHelloGenerator::ClientHelloGenerator(
	const MTPTlsClientHello &rules,
	bytes::const_span domain,
	bytes::const_span key)
: _domain(domain)
, _key(key)
, _greases(PrepareGreases()) {
	_result.reserve(kClientHelloLength);
	writeBlocks(rules.match([&](const MTPDtlsClientHello &data) {
		return data.vblocks().v;
	}));
	writePadding();
	writeDigest();
	writeTimestamp();
}

ClientHello ClientHelloGenerator::result() {
	return {
		_error ? QByteArray() : std::move(_result),
		_error ? QByteArray() : std::move(_digest) };
}

bytes::span ClientHelloGenerator::grow(int size) {
	if (_error
		|| size <= 0
		|| _result.size() + size > kClientHelloLength) {
		_error = true;
		return bytes::span();
	}

	const auto offset = _result.size();
	_result.resize(offset + size);
	return bytes::make_detached_span(_result).subspan(offset);
}

void ClientHelloGenerator::writeBlocks(const QVector<MTPTlsBlock> &blocks) {
	for (const auto &block : blocks) {
		writeBlock(block);
	}
}

void ClientHelloGenerator::writeBlock(const MTPTlsBlock &data) {
	data.match([&](const auto &data) {
		writeBlock(data);
	});
}

void ClientHelloGenerator::writeBlock(const MTPDtlsBlockString &data) {
	const auto &bytes = data.vdata().v;
	const auto storage = grow(bytes.size());
	if (storage.empty()) {
		return;
	}
	bytes::copy(storage, bytes::make_span(bytes));
}

void ClientHelloGenerator::writeBlock(const MTPDtlsBlockZero &data) {
	const auto length = data.vlength().v;
	const auto already = _result.size();
	const auto storage = grow(length);
	if (storage.empty()) {
		return;
	}
	if (length == kHelloDigestLength && _digestPosition < 0) {
		_digestPosition = already;
	}
	bytes::set_with_const(storage, bytes::type(0));
}

void ClientHelloGenerator::writeBlock(const MTPDtlsBlockGrease &data) {
	const auto seed = data.vseed().v;
	if (seed < 0 || seed >= _greases.size()) {
		_error = true;
		return;
	}
	const auto storage = grow(2);
	if (storage.empty()) {
		return;
	}
	bytes::set_with_const(storage, _greases[seed]);
}

void ClientHelloGenerator::writeBlock(const MTPDtlsBlockRandom &data) {
	const auto length = data.vlength().v;
	const auto storage = grow(length);
	if (storage.empty()) {
		return;
	}
	bytes::set_random(storage);
}

void ClientHelloGenerator::writeBlock(const MTPDtlsBlockDomain &data) {
	const auto storage = grow(_domain.size());
	if (storage.empty()) {
		return;
	}
	bytes::copy(storage, _domain);
}

void ClientHelloGenerator::writeBlock(const MTPDtlsBlockPublicKey &data) {
	const auto key = GeneratePublicKey();
	const auto storage = grow(key.size());
	if (storage.empty()) {
		return;
	}
	bytes::copy(storage, key);
}

void ClientHelloGenerator::writeBlock(const MTPDtlsBlockScope &data) {
	const auto storage = grow(kLengthSize);
	if (storage.empty()) {
		return;
	}
	const auto already = _result.size();
	writeBlocks(data.ventries().v);
	const auto length = qToBigEndian(uint16(_result.size() - already));
	bytes::copy(storage, bytes::object_as_span(&length));
}

void ClientHelloGenerator::writePadding() {
	const auto padding = kClientHelloLength - kLengthSize - _result.size();
	writeBlock(MTP_tlsBlockScope(
		MTP_vector<MTPTlsBlock>(1, MTP_tlsBlockZero(MTP_int(padding)))));
}

void ClientHelloGenerator::writeDigest() {
	if (_digestPosition < 0) {
		_error = true;
		return;
	}
	bytes::copy(
		bytes::make_detached_span(_result).subspan(_digestPosition),
		openssl::HmacSha256(_key, bytes::make_span(_result)));
}

void ClientHelloGenerator::writeTimestamp() {
	if (_digestPosition < 0) {
		_error = true;
		return;
	}
	const auto storage = bytes::make_detached_span(_result).subspan(
		_digestPosition + kHelloDigestLength - sizeof(int32),
		sizeof(int32));
	auto already = int32();
	bytes::copy(bytes::object_as_span(&already), storage);
	already ^= qToLittleEndian(int32(base::unixtime::http_now()));
	bytes::copy(storage, bytes::object_as_span(&already));

	_digest = QByteArray(kHelloDigestLength, Qt::Uninitialized);
	bytes::copy(
		bytes::make_detached_span(_digest),
		bytes::make_detached_span(_result).subspan(
			_digestPosition,
			kHelloDigestLength));
}

[[nodiscard]] ClientHello PrepareClientHello(
		const MTPTlsClientHello &rules,
		bytes::const_span domain,
		bytes::const_span key) {
	return ClientHelloGenerator(rules, domain, key).result();
}

[[nodiscard]] bool CheckPart(bytes::const_span data, QLatin1String check) {
	if (data.size() < check.size()) {
		return false;
	}
	return !bytes::compare(
		data.subspan(0, check.size()),
		bytes::make_span(check.data(), check.size()));
}

[[nodiscard]] int ReadPartLength(bytes::const_span data, int offset) {
	const auto storage = data.subspan(offset, kLengthSize);
	return qFromBigEndian(
		*reinterpret_cast<const uint16*>(storage.data()));
}

} // namespace

TlsSocket::TlsSocket(
	not_null<QThread*> thread,
	const bytes::vector &secret,
	const QNetworkProxy &proxy)
: AbstractSocket(thread)
, _secret(secret) {
	Expects(_secret.size() >= 21 && _secret[0] == bytes::type(0xEE));

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
		wrap([=] { plainConnected(); }));
	connect(
		&_socket,
		&QTcpSocket::disconnected,
		wrap([=] { plainDisconnected(); }));
	connect(
		&_socket,
		&QTcpSocket::readyRead,
		wrap([=] { plainReadyRead(); }));
	connect(
		&_socket,
		base::QTcpSocket_error,
		wrap([=](Error e) { handleError(e); }));
}

bytes::const_span TlsSocket::domainFromSecret() const {
	return bytes::make_span(_secret).subspan(17);
}

bytes::const_span TlsSocket::keyFromSecret() const {
	return bytes::make_span(_secret).subspan(1, 16);
}

void TlsSocket::plainConnected() {
	if (_state != State::Connecting) {
		return;
	}

	static const auto kClientHelloRules = PrepareClientHelloRules();
	const auto hello = PrepareClientHello(
		kClientHelloRules,
		domainFromSecret(),
		keyFromSecret());
	if (hello.data.isEmpty()) {
		LOG(("TLS Error: Could not generate Client Hello!"));
		_state = State::Error;
		_error.fire({});
	} else {
		_state = State::WaitingHello;
		_incoming = hello.digest;
		_socket.write(hello.data);
	}
}

void TlsSocket::plainDisconnected() {
	_state = State::NotConnected;
	_incoming = QByteArray();
	_serverHelloLength = 0;
	_incomingGoodDataOffset = 0;
	_incomingGoodDataLimit = 0;
	_disconnected.fire({});
}

void TlsSocket::plainReadyRead() {
	switch (_state) {
	case State::WaitingHello: return readHello();
	case State::Connected: return readData();
	}
}

bool TlsSocket::requiredHelloPartReady() const {
	return _incoming.size() >= kHelloDigestLength + _serverHelloLength;
}

void TlsSocket::readHello() {
	const auto parts1Size = kServerHelloPart1.size() + kLengthSize;
	if (!_serverHelloLength) {
		_serverHelloLength = parts1Size;
	}
	while (!requiredHelloPartReady()) {
		if (!_socket.bytesAvailable()) {
			return;
		}
		_incoming.append(_socket.readAll());
	}
	checkHelloParts12(parts1Size);
}

void TlsSocket::checkHelloParts12(int parts1Size) {
	const auto data = bytes::make_span(_incoming).subspan(
		kHelloDigestLength,
		parts1Size);
	const auto part2Size = ReadPartLength(data, parts1Size - kLengthSize);
	const auto parts123Size = parts1Size
		+ part2Size
		+ kServerHelloPart3.size()
		+ kLengthSize;
	if (_serverHelloLength == parts1Size) {
		const auto part1Offset = parts1Size
			- kLengthSize
			- kServerHelloPart1.size();
		if (!CheckPart(data.subspan(part1Offset), kServerHelloPart1)) {
			LOG(("TLS Error: Bad Server Hello part1."));
			handleError();
			return;
		}
		_serverHelloLength = parts123Size;
		if (!requiredHelloPartReady()) {
			readHello();
			return;
		}
	}
	checkHelloParts34(parts123Size);
}

void TlsSocket::checkHelloParts34(int parts123Size) {
	const auto data = bytes::make_span(_incoming).subspan(
		kHelloDigestLength,
		parts123Size);
	const auto part4Size = ReadPartLength(data, parts123Size - kLengthSize);
	const auto full = parts123Size + part4Size;
	if (_serverHelloLength == parts123Size) {
		const auto part3Offset = parts123Size
			- kLengthSize
			- kServerHelloPart3.size();
		if (!CheckPart(data.subspan(part3Offset), kServerHelloPart3)) {
			LOG(("TLS Error: Bad Server Hello part."));
			handleError();
			return;
		}
		_serverHelloLength = full;
		if (!requiredHelloPartReady()) {
			readHello();
			return;
		}
	}
	checkHelloDigest();
}

void TlsSocket::checkHelloDigest() {
	const auto fulldata = bytes::make_detached_span(_incoming).subspan(
		0,
		kHelloDigestLength + _serverHelloLength);
	const auto digest = fulldata.subspan(
		kHelloDigestLength + kServerHelloDigestPosition,
		kHelloDigestLength);
	const auto digestCopy = bytes::make_vector(digest);
	bytes::set_with_const(digest, bytes::type(0));
	const auto check = openssl::HmacSha256(keyFromSecret(), fulldata);
	if (bytes::compare(digestCopy, check) != 0) {
		LOG(("TLS Error: Bad Server Hello digest."));
		handleError();
		return;
	}
	shiftIncomingBy(fulldata.size());
	if (!_incoming.isEmpty()) {
		InvokeQueued(this, [=] {
			if (!checkNextPacket()) {
				handleError();
			}
		});
	}
	_incomingGoodDataOffset = _incomingGoodDataLimit = 0;
	_state = State::Connected;
	_connected.fire({});
}

void TlsSocket::readData() {
	if (!isConnected()) {
		return;
	}
	_incoming.append(_socket.readAll());
	if (!checkNextPacket()) {
		handleError();
	} else if (hasBytesAvailable()) {
		_readyRead.fire({});
	}
}

bool TlsSocket::checkNextPacket() {
	auto offset = 0;
	const auto incoming = bytes::make_span(_incoming);
	while (!_incomingGoodDataLimit) {
		const auto fullHeader = kServerHeader.size() + kLengthSize;
		if (incoming.size() <= offset + fullHeader) {
			return true;
		}
		if (!CheckPart(incoming.subspan(offset), kServerHeader)) {
			LOG(("TLS Error: Bad packet header."));
			return false;
		}
		const auto length = ReadPartLength(
			incoming,
			offset + kServerHeader.size());
		if (length > 0) {
			if (offset > 0) {
				shiftIncomingBy(offset);
			}
			_incomingGoodDataOffset = fullHeader;
			_incomingGoodDataLimit = length;
		} else {
			offset += kServerHeader.size() + kLengthSize + length;
		}
	}
	return true;
}

void TlsSocket::shiftIncomingBy(int amount) {
	Expects(_incomingGoodDataOffset == 0);
	Expects(_incomingGoodDataLimit == 0);

	const auto incoming = bytes::make_detached_span(_incoming);
	if (incoming.size() > amount) {
		bytes::move(incoming, incoming.subspan(amount));
		_incoming.chop(amount);
	} else {
		_incoming.clear();
	}
}

void TlsSocket::connectToHost(const QString &address, int port) {
	Expects(_state == State::NotConnected);

	_state = State::Connecting;
	_socket.connectToHost(address, port);
}

bool TlsSocket::isGoodStartNonce(bytes::const_span nonce) {
	return true;
}

void TlsSocket::timedOut() {
	_syncTimeRequests.fire({});
}

bool TlsSocket::isConnected() {
	return (_state == State::Connected);
}

bool TlsSocket::hasBytesAvailable() {
	return (_incomingGoodDataLimit > 0)
		&& (_incomingGoodDataOffset < _incoming.size());
}

int64 TlsSocket::read(bytes::span buffer) {
	auto written = int64(0);
	while (_incomingGoodDataLimit) {
		const auto available = std::min(
			_incomingGoodDataLimit,
			_incoming.size() - _incomingGoodDataOffset);
		if (available <= 0) {
			return written;
		}
		const auto write = std::min(std::size_t(available), buffer.size());
		if (write <= 0) {
			return written;
		}
		bytes::copy(
			buffer,
			bytes::make_span(_incoming).subspan(
				_incomingGoodDataOffset,
				write));
		written += write;
		buffer = buffer.subspan(write);
		_incomingGoodDataLimit -= write;
		_incomingGoodDataOffset += write;
		if (_incomingGoodDataLimit) {
			return written;
		}
		shiftIncomingBy(base::take(_incomingGoodDataOffset));
		if (!checkNextPacket()) {
			_state = State::Error;
			InvokeQueued(this, [=] { handleError(); });
			return written;
		}
	}
	return written;
}

void TlsSocket::write(bytes::const_span prefix, bytes::const_span buffer) {
	Expects(!buffer.empty());

	if (!isConnected()) {
		return;
	}
	if (!prefix.empty()) {
		_socket.write(kClientPrefix.data(), kClientPrefix.size());
	}
	while (!buffer.empty()) {
		const auto write = std::min(
			kClientPartSize - prefix.size(),
			buffer.size());
		_socket.write(kClientHeader.data(), kClientHeader.size());
		const auto size = qToBigEndian(uint16(prefix.size() + write));
		_socket.write(reinterpret_cast<const char*>(&size), sizeof(size));
		if (!prefix.empty()) {
			_socket.write(
				reinterpret_cast<const char*>(prefix.data()),
				prefix.size());
			prefix = bytes::const_span();
		}
		_socket.write(
			reinterpret_cast<const char*>(buffer.data()),
			write);
		buffer = buffer.subspan(write);
	}
}

int32 TlsSocket::debugState() {
	return _socket.state();
}

void TlsSocket::handleError(int errorCode) {
	if (_state != State::Connected) {
		_syncTimeRequests.fire({});
	}
	if (errorCode) {
		TcpSocket::LogError(errorCode, _socket.errorString());
	}
	_state = State::Error;
	_error.fire({});
}

} // namespace MTP::details
