/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/bytes.h"

namespace openssl {
class BigNum;
} // namespace openssl

namespace MTP {

struct ModExpFirst {
	static constexpr auto kRandomPowerSize = 256;

	bytes::vector modexp;
	bytes::vector randomPower;
};

[[nodiscard]] bool IsPrimeAndGood(bytes::const_span primeBytes, int g);
[[nodiscard]] bool IsGoodModExpFirst(
	const openssl::BigNum &modexp,
	const openssl::BigNum &prime);
[[nodiscard]] ModExpFirst CreateModExp(
	int g,
	bytes::const_span primeBytes,
	bytes::const_span randomSeed);
[[nodiscard]] bytes::vector CreateAuthKey(
	bytes::const_span firstBytes,
	bytes::const_span randomBytes,
	bytes::const_span primeBytes);

} // namespace MTP
