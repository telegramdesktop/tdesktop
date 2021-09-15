/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/mtproto_dh_utils.h"

#include "base/openssl_help.h"

namespace MTP {
namespace {

constexpr auto kMaxModExpSize = 256;

bool IsPrimeAndGoodCheck(const openssl::BigNum &prime, int g) {
	constexpr auto kGoodPrimeBitsCount = 2048;

	if (prime.failed()
		|| prime.isNegative()
		|| prime.bitsSize() != kGoodPrimeBitsCount) {
		LOG(("MTP Error: Bad prime bits count %1, expected %2."
			).arg(prime.bitsSize()
			).arg(kGoodPrimeBitsCount));
		return false;
	}

	const auto context = openssl::Context();
	if (!prime.isPrime(context)) {
		LOG(("MTP Error: Bad prime."));
		return false;
	}

	switch (g) {
	case 2: {
		const auto mod8 = prime.countModWord(8);
		if (mod8 != 7) {
			LOG(("BigNum PT Error: bad g value: %1, mod8: %2").arg(g).arg(mod8));
			return false;
		}
	} break;
	case 3: {
		const auto mod3 = prime.countModWord(3);
		if (mod3 != 2) {
			LOG(("BigNum PT Error: bad g value: %1, mod3: %2").arg(g).arg(mod3));
			return false;
		}
	} break;
	case 4: break;
	case 5: {
		const auto mod5 = prime.countModWord(5);
		if (mod5 != 1 && mod5 != 4) {
			LOG(("BigNum PT Error: bad g value: %1, mod5: %2").arg(g).arg(mod5));
			return false;
		}
	} break;
	case 6: {
		const auto mod24 = prime.countModWord(24);
		if (mod24 != 19 && mod24 != 23) {
			LOG(("BigNum PT Error: bad g value: %1, mod24: %2").arg(g).arg(mod24));
			return false;
		}
	} break;
	case 7: {
		const auto mod7 = prime.countModWord(7);
		if (mod7 != 3 && mod7 != 5 && mod7 != 6) {
			LOG(("BigNum PT Error: bad g value: %1, mod7: %2").arg(g).arg(mod7));
			return false;
		}
	} break;
	default: {
		LOG(("BigNum PT Error: bad g value: %1").arg(g));
		return false;
	} break;
	}

	if (!openssl::BigNum(prime).subWord(1).divWord(2).isPrime(context)) {
		LOG(("MTP Error: Bad (prime - 1) / 2."));
		return false;
	}

	return true;
}

} // namespace

bool IsGoodModExpFirst(
		const openssl::BigNum &modexp,
		const openssl::BigNum &prime) {
	const auto diff = openssl::BigNum::Sub(prime, modexp);
	if (modexp.failed() || prime.failed() || diff.failed()) {
		return false;
	}
	constexpr auto kMinDiffBitsCount = 2048 - 64;
	if (diff.isNegative()
		|| diff.bitsSize() < kMinDiffBitsCount
		|| modexp.bitsSize() < kMinDiffBitsCount
		|| modexp.bytesSize() > kMaxModExpSize) {
		return false;
	}
	return true;
}

bool IsPrimeAndGood(bytes::const_span primeBytes, int g) {
	static constexpr unsigned char GoodPrime[] = {
		0xC7, 0x1C, 0xAE, 0xB9, 0xC6, 0xB1, 0xC9, 0x04, 0x8E, 0x6C, 0x52, 0x2F, 0x70, 0xF1, 0x3F, 0x73,
		0x98, 0x0D, 0x40, 0x23, 0x8E, 0x3E, 0x21, 0xC1, 0x49, 0x34, 0xD0, 0x37, 0x56, 0x3D, 0x93, 0x0F,
		0x48, 0x19, 0x8A, 0x0A, 0xA7, 0xC1, 0x40, 0x58, 0x22, 0x94, 0x93, 0xD2, 0x25, 0x30, 0xF4, 0xDB,
		0xFA, 0x33, 0x6F, 0x6E, 0x0A, 0xC9, 0x25, 0x13, 0x95, 0x43, 0xAE, 0xD4, 0x4C, 0xCE, 0x7C, 0x37,
		0x20, 0xFD, 0x51, 0xF6, 0x94, 0x58, 0x70, 0x5A, 0xC6, 0x8C, 0xD4, 0xFE, 0x6B, 0x6B, 0x13, 0xAB,
		0xDC, 0x97, 0x46, 0x51, 0x29, 0x69, 0x32, 0x84, 0x54, 0xF1, 0x8F, 0xAF, 0x8C, 0x59, 0x5F, 0x64,
		0x24, 0x77, 0xFE, 0x96, 0xBB, 0x2A, 0x94, 0x1D, 0x5B, 0xCD, 0x1D, 0x4A, 0xC8, 0xCC, 0x49, 0x88,
		0x07, 0x08, 0xFA, 0x9B, 0x37, 0x8E, 0x3C, 0x4F, 0x3A, 0x90, 0x60, 0xBE, 0xE6, 0x7C, 0xF9, 0xA4,
		0xA4, 0xA6, 0x95, 0x81, 0x10, 0x51, 0x90, 0x7E, 0x16, 0x27, 0x53, 0xB5, 0x6B, 0x0F, 0x6B, 0x41,
		0x0D, 0xBA, 0x74, 0xD8, 0xA8, 0x4B, 0x2A, 0x14, 0xB3, 0x14, 0x4E, 0x0E, 0xF1, 0x28, 0x47, 0x54,
		0xFD, 0x17, 0xED, 0x95, 0x0D, 0x59, 0x65, 0xB4, 0xB9, 0xDD, 0x46, 0x58, 0x2D, 0xB1, 0x17, 0x8D,
		0x16, 0x9C, 0x6B, 0xC4, 0x65, 0xB0, 0xD6, 0xFF, 0x9C, 0xA3, 0x92, 0x8F, 0xEF, 0x5B, 0x9A, 0xE4,
		0xE4, 0x18, 0xFC, 0x15, 0xE8, 0x3E, 0xBE, 0xA0, 0xF8, 0x7F, 0xA9, 0xFF, 0x5E, 0xED, 0x70, 0x05,
		0x0D, 0xED, 0x28, 0x49, 0xF4, 0x7B, 0xF9, 0x59, 0xD9, 0x56, 0x85, 0x0C, 0xE9, 0x29, 0x85, 0x1F,
		0x0D, 0x81, 0x15, 0xF6, 0x35, 0xB1, 0x05, 0xEE, 0x2E, 0x4E, 0x15, 0xD0, 0x4B, 0x24, 0x54, 0xBF,
		0x6F, 0x4F, 0xAD, 0xF0, 0x34, 0xB1, 0x04, 0x03, 0x11, 0x9C, 0xD8, 0xE3, 0xB9, 0x2F, 0xCC, 0x5B };

	if (!bytes::compare(bytes::make_span(GoodPrime), primeBytes)) {
		if (g == 3 || g == 4 || g == 5 || g == 7) {
			return true;
		}
	}

	return IsPrimeAndGoodCheck(openssl::BigNum(primeBytes), g);
}

ModExpFirst CreateModExp(
		int g,
		bytes::const_span primeBytes,
		bytes::const_span randomSeed) {
	Expects(randomSeed.size() == ModExpFirst::kRandomPowerSize);

	using namespace openssl;

	BigNum prime(primeBytes);
	auto result = ModExpFirst();
	result.randomPower.resize(ModExpFirst::kRandomPowerSize);
	while (true) {
		bytes::set_random(result.randomPower);
		for (auto i = 0; i != ModExpFirst::kRandomPowerSize; ++i) {
			result.randomPower[i] ^= randomSeed[i];
		}
		const auto modexp = BigNum::ModExp(
			BigNum(g),
			BigNum(result.randomPower),
			prime);
		if (IsGoodModExpFirst(modexp, prime)) {
			result.modexp = modexp.getBytes();
			return result;
		}
	}
}

bytes::vector CreateAuthKey(
		bytes::const_span firstBytes,
		bytes::const_span randomBytes,
		bytes::const_span primeBytes) {
	using openssl::BigNum;

	const auto first = BigNum(firstBytes);
	const auto prime = BigNum(primeBytes);
	if (!IsGoodModExpFirst(first, prime)) {
		LOG(("AuthKey Error: Bad first prime in CreateAuthKey()."));
		return {};
	}
	return BigNum::ModExp(first, BigNum(randomBytes), prime).getBytes();
}

} // namespace MTP
