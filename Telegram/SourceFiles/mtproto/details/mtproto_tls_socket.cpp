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

#include <QtCore/QtEndian>
#include <range/v3/algorithm/reverse.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/algorithm/shuffle.hpp>
#include <random>

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

static std::mt19937 kRng(std::random_device{}());

[[nodiscard]] MTPTlsClientHello PrepareClientHelloRulesChrome();
[[nodiscard]] MTPTlsClientHello PrepareClientHelloRulesFirefox();

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
	for (auto i = 0; i != kMaxGrease; ++i) {
		const auto j = (uchar(result[i]) * 0x9E3779B9) % kMaxGrease;
		if (i != j) {
			std::swap(result[i], result[j]);
		}
	}
	return result;
}

[[nodiscard]] MTPTlsClientHello PrepareClientHelloRulesChrome() {
	using Scope = QVector<MTPTlsBlock>;
	using Permutation = std::vector<Scope>;
	using StackElement = std::variant<Scope, Permutation>;
	auto stack = std::vector<StackElement>();
	const auto pushToBack = [&](MTPTlsBlock &&block) {
		Expects(!stack.empty());

		if (const auto scope = std::get_if<Scope>(&stack.back())) {
			scope->push_back(std::move(block));
		} else {
			auto &permutation = v::get<Permutation>(stack.back());
			Assert(!permutation.empty());
			permutation.back().push_back(std::move(block));
		}
	};
	const auto S = [&](QByteArray data) {
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
	const auto OpenScope = [&] {
		stack.emplace_back(Scope());
	};
	const auto CloseScope = [&] {
		Expects(stack.size() > 1);
		Expects(v::is<Scope>(stack.back()));

		const auto blocks = std::move(v::get<Scope>(stack.back()));
		stack.pop_back();
		pushToBack(MTP_tlsBlockScope(MTP_vector<MTPTlsBlock>(blocks)));
	};
	const auto OpenPermutation = [&] {
		stack.emplace_back(Permutation());
	};
	const auto ClosePermutation = [&] {
		Expects(stack.size() > 1);
		Expects(v::is<Permutation>(stack.back()));

		auto list = std::move(v::get<Permutation>(stack.back()));
		stack.pop_back();

		ranges::shuffle(list, kRng);

		const auto wrapped = list | ranges::views::transform([](
				const QVector<MTPTlsBlock> &elements) {
			return MTP_vector<MTPTlsBlock>(elements);
		}) | ranges::to<QVector<MTPVector<MTPTlsBlock>>>();

		pushToBack(MTP_tlsBlockPermutation(
			MTP_vector<MTPVector<MTPTlsBlock>>(wrapped)));
	};
	const auto StartPermutationElement = [&] {
		Expects(stack.size() > 1);
		Expects(v::is<Permutation>(stack.back()));

		v::get<Permutation>(stack.back()).emplace_back();
	};
	const auto Finish = [&] {
		Expects(stack.size() == 1);
		Expects(v::is<Scope>(stack.back()));

		return v::get<Scope>(stack.back());
	};

	stack.emplace_back(Scope());

	S("\x16\x03\x01\x02\x00\x01\x00\x01\xfc\x03\x03"_q);
	Z(32);
	S("\x20"_q);
	R(32);

	const auto allCiphers = std::vector<QByteArray>{
		"\x13\x01"_q,
		"\x13\x03"_q,
		"\x13\x02"_q,
		"\xc0\x2b"_q,
		"\xc0\x2f"_q,
		"\xcc\xa9"_q,
		"\xcc\xa8"_q,
		"\xc0\x2c"_q,
		"\xc0\x30"_q,
		"\xc0\x14"_q,
		"\x00\x9c"_q,
		"\x00\x9d"_q,
		"\x00\x2f"_q,
		"\x00\x35"_q,
		"\xc0\x13"_q,
		"\x00\x3c"_q,
	};
	
	auto selected = std::vector<QByteArray>{
		"\x13\x01"_q,
		"\x13\x03"_q,
		"\x13\x02"_q,
	};
	
	auto rng = std::mt19937(std::random_device{}());
	auto countDist = std::uniform_int_distribution<int>(1, 9);
	auto extraCount = countDist(rng);
	
	auto available = std::vector<QByteArray>(allCiphers.begin() + 3, allCiphers.end());
	ranges::shuffle(available, rng);
	
	for (int i = 0; i < extraCount && i < (int)available.size(); ++i) {
		selected.push_back(available[i]);
	}
	
	auto tls13 = std::vector<QByteArray>(selected.begin(), selected.begin() + 3);
	auto others = std::vector<QByteArray>(selected.begin() + 3, selected.end());
	ranges::shuffle(others, rng);
	
	auto finalCiphers = tls13;
	finalCiphers.insert(finalCiphers.end(), others.begin(), others.end());
	
	QByteArray cipherData;
	for (const auto &c : finalCiphers) {
		cipherData.append(c);
	}
	
	uint16 cipherLen = qToBigEndian(uint16(cipherData.size()));
	S(QByteArray(reinterpret_cast<const char*>(&cipherLen), 2));
	S(cipherData);

	S("\x01\x00"_q);
	OpenScope();

	S("\x00\x00"_q);
	OpenScope();
	OpenScope();
	S("\x00"_q);
	OpenScope();
	D();
	CloseScope();
	CloseScope();
	CloseScope();

	struct ExtInfo {
		QByteArray data;
	};
	
	auto extraExts = std::vector<ExtInfo>();
	
	extraExts.push_back({"\x00\x17\x00\x00"_q});
	extraExts.push_back({"\xff\x01\x00\x01\x00"_q});
	extraExts.push_back({"\x00\x23\x00\x00"_q});
	extraExts.push_back({"\x00\x12\x00\x00"_q});
	extraExts.push_back({"\x00\x0b\x00\x02\x01\x00"_q});
	extraExts.push_back({"\x00\x2d\x00\x02\x01\x01"_q});

	ranges::shuffle(extraExts, rng);
	auto extCount = std::uniform_int_distribution<int>(0, 5)(rng);
	
	for (int i = 0; i < extCount && i < (int)extraExts.size(); ++i) {
		S(extraExts[i].data);
	}

	CloseScope();

	return MTP_tlsClientHello(MTP_vector<MTPTlsBlock>(Finish()));
}

[[nodiscard]] MTPTlsClientHello PrepareClientHelloRulesFirefox() {
	
	using Scope = QVector<MTPTlsBlock>;
	using Permutation = std::vector<Scope>;
	using StackElement = std::variant<Scope, Permutation>;
	auto stack = std::vector<StackElement>();
	const auto pushToBack = [&](MTPTlsBlock &&block) {
		Expects(!stack.empty());

		if (const auto scope = std::get_if<Scope>(&stack.back())) {
			scope->push_back(std::move(block));
		} else {
			auto &permutation = v::get<Permutation>(stack.back());
			Assert(!permutation.empty());
			permutation.back().push_back(std::move(block));
		}
	};
	const auto S = [&](QByteArray data) {
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
	const auto OpenScope = [&] {
		stack.emplace_back(Scope());
	};
	const auto CloseScope = [&] {
		Expects(stack.size() > 1);
		Expects(v::is<Scope>(stack.back()));

		const auto blocks = std::move(v::get<Scope>(stack.back()));
		stack.pop_back();
		pushToBack(MTP_tlsBlockScope(MTP_vector<MTPTlsBlock>(blocks)));
	};
	const auto OpenPermutation = [&] {
		stack.emplace_back(Permutation());
	};
	const auto ClosePermutation = [&] {
		Expects(stack.size() > 1);
		Expects(v::is<Permutation>(stack.back()));

		auto list = std::move(v::get<Permutation>(stack.back()));
		stack.pop_back();

		ranges::shuffle(list, kRng);

		const auto wrapped = list | ranges::views::transform([](
				const QVector<MTPTlsBlock> &elements) {
			return MTP_vector<MTPTlsBlock>(elements);
		}) | ranges::to<QVector<MTPVector<MTPTlsBlock>>>();

		pushToBack(MTP_tlsBlockPermutation(
			MTP_vector<MTPVector<MTPTlsBlock>>(wrapped)));
	};
	const auto StartPermutationElement = [&] {
		Expects(stack.size() > 1);
		Expects(v::is<Permutation>(stack.back()));

		v::get<Permutation>(stack.back()).emplace_back();
	};
	const auto Finish = [&] {
		Expects(stack.size() == 1);
		Expects(v::is<Scope>(stack.back()));

		return v::get<Scope>(stack.back());
	};

	stack.emplace_back(Scope());

	S("\x16\x03\x01\x02\x00\x01\x00\x01\xfc\x03\x03"_q);
	Z(32);
	S("\x20"_q);
	R(32);
	
	const auto allCiphers = std::vector<QByteArray>{
		"\x13\x01"_q,
		"\x13\x03"_q,
		"\x13\x02"_q,
		"\xc0\x2b"_q,
		"\xc0\x2f"_q,
		"\xcc\xa9"_q,
		"\xcc\xa8"_q,
		"\xc0\x2c"_q,
		"\xc0\x30"_q,
		"\xc0\x13"_q,
		"\xc0\x14"_q,
		"\x00\x9c"_q,
		"\x00\x9d"_q,
		"\x00\x2f"_q,
		"\x00\x35"_q,
	};
	
	auto selected = std::vector<QByteArray>{
		"\x13\x01"_q,
		"\x13\x03"_q,
		"\x13\x02"_q,
	};
	
	auto rng = std::mt19937(std::random_device{}());
	auto countDist = std::uniform_int_distribution<int>(1, 11);
	auto extraCount = countDist(rng);
	
	auto available = std::vector<QByteArray>(allCiphers.begin() + 3, allCiphers.end());
	ranges::shuffle(available, rng);
	
	for (int i = 0; i < extraCount && i < (int)available.size(); ++i) {
		selected.push_back(available[i]);
	}
	
	auto tls13 = std::vector<QByteArray>(selected.begin(), selected.begin() + 3);
	auto others = std::vector<QByteArray>(selected.begin() + 3, selected.end());
	ranges::shuffle(others, rng);
	
	auto finalCiphers = tls13;
	finalCiphers.insert(finalCiphers.end(), others.begin(), others.end());
	
	QByteArray cipherData;
	for (const auto &c : finalCiphers) {
		cipherData.append(c);
	}
	
	uint16 cipherLen = qToBigEndian(uint16(cipherData.size()));
	S(QByteArray(reinterpret_cast<const char*>(&cipherLen), 2));
	S(cipherData);

	S("\x01\x00"_q);
	OpenScope();

	S("\x00\x00"_q);
	OpenScope();
	OpenScope();
	S("\x00"_q);
	OpenScope();
	D();
	CloseScope();
	CloseScope();
	CloseScope();

	auto extraExts = std::vector<QByteArray>{
		"\x00\x17\x00\x00"_q,
		"\xff\x01\x00\x01\x00"_q,
		"\x00\x23\x00\x00"_q,
		"\x00\x12\x00\x00"_q,
		"\x00\x0b\x00\x02\x01\x00"_q,
		"\x00\x2d\x00\x02\x01\x01"_q,
		"\x00\x1c\x00\x02\x40\x01"_q,
	};
	
	ranges::shuffle(extraExts, rng);
	auto extCount = std::uniform_int_distribution<int>(1, 6)(rng);
	
	for (int i = 0; i < extCount && i < (int)extraExts.size(); ++i) {
		S(extraExts[i]);
	}

	CloseScope();

	return MTP_tlsClientHello(MTP_vector<MTPTlsBlock>(Finish()));
}

[[nodiscard]] MTPTlsClientHello PrepareClientHelloRules() {
	auto rng = std::mt19937(std::random_device{}());
	std::uniform_int_distribution<int> dist(0, 1);
	
	return (dist(rng) == 0) 
		? PrepareClientHelloRulesChrome() 
		: PrepareClientHelloRulesFirefox();
}


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


	return result;
}

struct ClientHello {
	QByteArray data;
	QByteArray digest;
};

class Generator {
public:
	Generator(
		const MTPTlsClientHello &rules,
		bytes::const_span domain,
		bytes::const_span key);
	[[nodiscard]] ClientHello take();

private:
	class Part final {
	public:
		explicit Part(
			bytes::const_span domain,
			const bytes::vector &greases);

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
		void writeBlock(const MTPDtlsBlockPermutation &data);
		void writeBlock(const MTPDtlsBlockM &data);
		void writeBlock(const MTPDtlsBlockE &data);
		void writeBlock(const MTPDtlsBlockPadding &data);
		void finalize(bytes::const_span key);
		[[nodiscard]] QByteArray extractDigest() const;

		[[nodiscard]] bool error() const;
		[[nodiscard]] QByteArray take();

	private:
		void writePadding();
		void writeDigest(bytes::const_span key);
		void injectTimestamp();

		bytes::const_span _domain;
		const bytes::vector &_greases;
		QByteArray _result;
		const char *_data = nullptr;
		int _digestPosition = -1;
		bool _error = false;

	};

	bytes::vector _greases;
	Part _result;
	QByteArray _digest;

};

Generator::Part::Part(
	bytes::const_span domain,
	const bytes::vector &greases)
: _domain(domain)
, _greases(greases) {
	_result.reserve(kClientHelloLength);
	_data = _result.constData();
}

bool Generator::Part::error() const {
	return _error;
}

QByteArray Generator::Part::take() {
	Expects(_error || _result.constData() == _data);

	return _error ? QByteArray() : std::move(_result);
}

bytes::span Generator::Part::grow(int size) {
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

void Generator::Part::writeBlocks(const QVector<MTPTlsBlock> &blocks) {
	for (const auto &block : blocks) {
		writeBlock(block);
	}
}

void Generator::Part::writeBlock(const MTPTlsBlock &data) {
    data.match(
        [&](const MTPDtlsBlockString &data) { writeBlock(data); },
        [&](const MTPDtlsBlockZero &data) { writeBlock(data); },
        [&](const MTPDtlsBlockGrease &data) { writeBlock(data); },
        [&](const MTPDtlsBlockRandom &data) { writeBlock(data); },
        [&](const MTPDtlsBlockDomain &data) { writeBlock(data); },
        [&](const MTPDtlsBlockPublicKey &data) { writeBlock(data); },
        [&](const MTPDtlsBlockScope &data) { writeBlock(data); },
        [&](const MTPDtlsBlockPermutation &data) { writeBlock(data); },
        [&](const MTPDtlsBlockM &data) { writeBlock(data); },
        [&](const MTPDtlsBlockE &data) { writeBlock(data); },
        [&](const MTPDtlsBlockPadding &data) { writeBlock(data); }
    );
}


void Generator::Part::writeBlock(const MTPDtlsBlockString &data) {
	const auto &bytes = data.vdata().v;
	const auto storage = grow(bytes.size());
	if (storage.empty()) {
		return;
	}
	bytes::copy(storage, bytes::make_span(bytes));
}

void Generator::Part::writeBlock(const MTPDtlsBlockZero &data) {
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

void Generator::Part::writeBlock(const MTPDtlsBlockGrease &data) {
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

void Generator::Part::writeBlock(const MTPDtlsBlockRandom &data) {
	const auto length = data.vlength().v;
	const auto storage = grow(length);
	if (storage.empty()) {
		return;
	}
	bytes::set_random(storage);
}

void Generator::Part::writeBlock(const MTPDtlsBlockDomain &data) {
	const auto storage = grow(_domain.size());
	if (storage.empty()) {
		return;
	}
	bytes::copy(storage, _domain);
}

void Generator::Part::writeBlock(const MTPDtlsBlockPublicKey &data) {
	const auto key = GeneratePublicKey();
	const auto storage = grow(key.size());
	if (storage.empty()) {
		return;
	}
	bytes::copy(storage, key);
}

void Generator::Part::writeBlock(const MTPDtlsBlockScope &data) {
	const auto storage = grow(kLengthSize);
	if (storage.empty()) {
		return;
	}
	const auto already = _result.size();
	writeBlocks(data.ventries().v);
	const auto length = qToBigEndian(uint16(_result.size() - already));
	bytes::copy(storage, bytes::object_as_span(&length));
}

void Generator::Part::writeBlock(const MTPDtlsBlockPermutation &data) {
	auto list = std::vector<QByteArray>();
	list.reserve(data.ventries().v.size());
	for (const auto &inner : data.ventries().v) {
		auto part = Part(_domain, _greases);
		part.writeBlocks(inner.v);
		if (part.error()) {
			_error = true;
			return;
		}
		list.push_back(part.take());
	}

	for (const auto &element : list) {
		const auto storage = grow(element.size());
		if (storage.empty()) {
			return;
		}
		bytes::copy(storage, bytes::make_span(element));
	}
}

void Generator::Part::writeBlock(const MTPDtlsBlockM &data) {
}
void Generator::Part::writeBlock(const MTPDtlsBlockE &data) {
}
void Generator::Part::writeBlock(const MTPDtlsBlockPadding &data) {
}

void Generator::Part::finalize(bytes::const_span key) {
	if (_error) {
		return;
	} else if (_digestPosition < 0) {
		_error = true;
		return;
	}
	writePadding();
	writeDigest(key);
	injectTimestamp();
}

QByteArray Generator::Part::extractDigest() const {
	if (_digestPosition < 0) {
		return {};
	}
	return _result.mid(_digestPosition, kHelloDigestLength);
}

void Generator::Part::writePadding() {
	Expects(_result.size() <= kClientHelloLength - kLengthSize);

	const auto padding = kClientHelloLength - kLengthSize - _result.size();
	writeBlock(MTP_tlsBlockScope(
		MTP_vector<MTPTlsBlock>(1, MTP_tlsBlockZero(MTP_int(padding)))));
}

void Generator::Part::writeDigest(bytes::const_span key) {
	Expects(_digestPosition >= 0);

	bytes::copy(
		bytes::make_detached_span(_result).subspan(_digestPosition),
		openssl::HmacSha256(key, bytes::make_span(_result)));
}

void Generator::Part::injectTimestamp() {
	Expects(_digestPosition >= 0);

	const auto storage = bytes::make_detached_span(_result).subspan(
		_digestPosition + kHelloDigestLength - sizeof(int32),
		sizeof(int32));
	auto already = int32();
	bytes::copy(bytes::object_as_span(&already), storage);
	auto randomJitter = int32(0);
	bytes::set_random(bytes::object_as_span(&randomJitter));
	randomJitter &= 0x3F;
	already ^= qToLittleEndian(int32(base::unixtime::http_now() + randomJitter));
	bytes::copy(storage, bytes::object_as_span(&already));
}

Generator::Generator(
	const MTPTlsClientHello &rules,
	bytes::const_span domain,
	bytes::const_span key)
: _greases(PrepareGreases())
, _result(domain, _greases) {
	_result.writeBlocks(rules.match([&](const MTPDtlsClientHello &data) {
		return data.vblocks().v;
	}));
	_result.finalize(key);
}

ClientHello Generator::take() {
	auto digest = _result.extractDigest();
	return { _result.take(), std::move(digest) };
}

[[nodiscard]] ClientHello PrepareClientHello(
		const MTPTlsClientHello &rules,
		bytes::const_span domain,
		bytes::const_span key) {
	return Generator(rules, domain, key).take();
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

}

TlsSocket::TlsSocket(
	not_null<QThread*> thread,
	const bytes::vector &secret,
	const QNetworkProxy &proxy,
	bool protocolForFiles)
: AbstractSocket(thread)
, _secret(secret) {
	Expects(_secret.size() >= 21 && _secret[0] == bytes::type(0xEE));

	_socket.moveToThread(thread);
	_socket.setProxy(proxy);
	_socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
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
		&QAbstractSocket::errorOccurred,
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

	const auto kClientHelloRules = PrepareClientHelloRules();
	const auto hello = PrepareClientHello(
		kClientHelloRules,
		domainFromSecret(),
		keyFromSecret());
	if (hello.data.isEmpty()) {
		logError(888, "Could not generate Client Hello.");
		_state = State::Error;
		_error.fire({});
	} else {
		_state = State::WaitingHello;
		_incoming = hello.digest;
		
		const auto &data = hello.data;
		int offset = 0;
		
		auto rng = std::mt19937(std::random_device{}());
		
		_socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
		
		auto chunkSizeDist = std::uniform_int_distribution<int>(16, 128);
		auto delayDist = std::uniform_int_distribution<int>(3, 25);
		
		QThread::msleep(std::uniform_int_distribution<int>(5, 100)(rng));
		
		auto firstChunkDist = std::uniform_int_distribution<int>(1, 32);
		const auto firstChunk = std::min(firstChunkDist(rng), data.size());
		_socket.write(data.mid(0, firstChunk));
		_socket.flush();
		offset = firstChunk;
		
		if (offset < data.size()) {
			QThread::msleep(delayDist(rng));
		}
		
		while (offset < data.size()) {
			const auto chunkSize = std::min(
				chunkSizeDist(rng),
				data.size() - offset);
			_socket.write(data.mid(offset, chunkSize));
			_socket.flush();
			offset += chunkSize;
			
			if (offset < data.size()) {
				QThread::msleep(delayDist(rng));
			}
		}
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
			logError(888, "Bad Server Hello part1.");
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
			logError(888, "Bad Server Hello part.");
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
		logError(888, "Bad Server Hello digest.");
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
			logError(888, "Bad packet header.");
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
			int(_incoming.size()) - _incomingGoodDataOffset);
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

QString TlsSocket::debugPostfix() const {
	return u"_ee"_q;
}

void TlsSocket::handleError(int errorCode) {
	if (_state != State::Connected) {
		_syncTimeRequests.fire({});
	}
	if (errorCode) {
		logError(errorCode, _socket.errorString());
	}
	_state = State::Error;
	_error.fire({});
}

} // namespace MTP::details
