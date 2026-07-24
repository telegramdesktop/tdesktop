/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/ton/gram_address.h"

#include "base/assertion.h"
#include "gram/ton/gram_crc.h"

namespace Gram {
namespace {

constexpr auto kBounceableTag = 0x11;
constexpr auto kNonBounceableTag = 0x51;
constexpr auto kTestnetFlag = 0x80;
constexpr auto kFriendlyLength = 48;
constexpr auto kPackedLength = 36;
constexpr auto kHashLength = 32;

[[nodiscard]] bool IsFriendlyCharset(const QString &text) {
	for (const auto ch : text) {
		const auto code = ch.unicode();
		const auto ascii = (code >= 'A' && code <= 'Z')
			|| (code >= 'a' && code <= 'z')
			|| (code >= '0' && code <= '9')
			|| code == '+'
			|| code == '/'
			|| code == '-'
			|| code == '_';
		if (!ascii) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] bool IsHash64(const QString &text) {
	if (text.size() != kHashLength * 2) {
		return false;
	}
	for (const auto ch : text) {
		const auto code = ch.unicode();
		const auto hex = (code >= '0' && code <= '9')
			|| (code >= 'a' && code <= 'f')
			|| (code >= 'A' && code <= 'F');
		if (!hex) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] std::optional<ParsedAddress> ParseFriendly(const QString &text) {
	auto normalized = text;
	normalized.replace(QChar('-'), QChar('+'));
	normalized.replace(QChar('_'), QChar('/'));

	const auto decoded = QByteArray::fromBase64Encoding(
		normalized.toLatin1(),
		QByteArray::Base64Encoding
			| QByteArray::AbortOnBase64DecodingErrors);
	if (!decoded || decoded.decoded.size() != kPackedLength) {
		return std::nullopt;
	}
	const auto &packed = decoded.decoded;

	const auto crc = Crc16(packed.left(kPackedLength - 2));
	if (uchar(packed[kPackedLength - 2]) != uchar((crc >> 8) & 0xFF)
		|| uchar(packed[kPackedLength - 1]) != uchar(crc & 0xFF)) {
		return std::nullopt;
	}

	auto tag = int(uchar(packed[0]));
	auto testnet = false;
	if (tag & kTestnetFlag) {
		testnet = true;
		tag &= ~kTestnetFlag;
	}
	auto bounceable = true;
	if (tag == kBounceableTag) {
		bounceable = true;
	} else if (tag == kNonBounceableTag) {
		bounceable = false;
	} else {
		return std::nullopt;
	}

	const auto wcByte = uchar(packed[1]);
	auto result = ParsedAddress();
	result.address.workchain = (wcByte == 0xFF) ? -1 : qint32(wcByte);
	result.address.hash = packed.mid(2, kHashLength);
	result.friendly = true;
	result.bounceable = bounceable;
	result.testnet = testnet;
	return result;
}

[[nodiscard]] std::optional<ParsedAddress> ParseRaw(const QString &text) {
	const auto colon = text.indexOf(QChar(':'));
	if (colon < 0) {
		return std::nullopt;
	}
	const auto workchainText = text.left(colon);
	const auto hashText = text.mid(colon + 1);
	auto ok = false;
	const auto workchain = workchainText.toInt(&ok, 10);
	if (!ok || !IsHash64(hashText)) {
		return std::nullopt;
	}
	auto result = ParsedAddress();
	result.address.workchain = workchain;
	result.address.hash = QByteArray::fromHex(hashText.toLatin1());
	result.friendly = false;
	result.bounceable = true;
	result.testnet = false;
	return result;
}

} // namespace

std::optional<ParsedAddress> ParseAddress(const QString &text) {
	if (text.size() == kFriendlyLength && IsFriendlyCharset(text)) {
		return ParseFriendly(text);
	}
	return ParseRaw(text);
}

QString FormatRaw(const Address &address) {
	return QString::number(address.workchain)
		+ u":"_q
		+ QString::fromLatin1(address.hash.toHex());
}

QString FormatFriendly(
		const Address &address,
		bool bounceable,
		bool testnet) {
	Expects(address.hash.size() == kHashLength);

	auto tag = bounceable ? kBounceableTag : kNonBounceableTag;
	if (testnet) {
		tag |= kTestnetFlag;
	}
	auto packed = QByteArray();
	packed.reserve(kPackedLength);
	packed.append(char(tag));
	packed.append(char(address.workchain & 0xFF));
	packed.append(address.hash);

	const auto crc = Crc16(packed);
	packed.append(char((crc >> 8) & 0xFF));
	packed.append(char(crc & 0xFF));

	return QString::fromLatin1(packed.toBase64(QByteArray::Base64UrlEncoding));
}

} // namespace Gram
