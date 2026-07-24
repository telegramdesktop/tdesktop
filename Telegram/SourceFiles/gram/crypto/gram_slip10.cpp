/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/crypto/gram_slip10.h"

#include "base/openssl_help.h"
#include "gram/crypto/gram_hmac.h"

namespace Gram {
namespace {

constexpr auto kHardenedOffset = quint32(0x80000000);
constexpr auto kKeySize = 32;

[[nodiscard]] HdKeyState SplitHmac(QByteArray &&i) {
	auto result = HdKeyState();
	result.key = i.left(kKeySize);
	result.chainCode = i.mid(kKeySize);
	OPENSSL_cleanse(i.data(), i.size());
	return result;
}

} // namespace

HdKeyState::~HdKeyState() {
	if (!key.isEmpty()) {
		OPENSSL_cleanse(key.data(), key.size());
	}
	if (!chainCode.isEmpty()) {
		OPENSSL_cleanse(chainCode.data(), chainCode.size());
	}
}

HdKeyState Ed25519MasterKey(const QByteArray &seed) {
	const auto curve = QByteArray("ed25519 seed");
	return SplitHmac(
		HmacSha512(bytes::make_span(curve), bytes::make_span(seed)));
}

HdKeyState DeriveHardened(const HdKeyState &parent, quint32 index) {
	Expects(index < kHardenedOffset);

	const auto value = index + kHardenedOffset;
	auto data = QByteArray();
	data.reserve(1 + kKeySize + 4);
	data.append(char(0));
	data.append(parent.key);
	data.append(char((value >> 24) & 0xFF));
	data.append(char((value >> 16) & 0xFF));
	data.append(char((value >> 8) & 0xFF));
	data.append(char(value & 0xFF));

	auto result = SplitHmac(
		HmacSha512(bytes::make_span(parent.chainCode), bytes::make_span(data)));
	OPENSSL_cleanse(data.data(), data.size());
	return result;
}

QByteArray DeriveEd25519Path(
		const QByteArray &seed,
		std::initializer_list<quint32> path) {
	auto state = Ed25519MasterKey(seed);
	for (const auto index : path) {
		state = DeriveHardened(state, index);
	}
	return std::move(state.key);
}

} // namespace Gram
