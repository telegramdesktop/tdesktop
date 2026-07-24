/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/crypto/gram_hmac.h"

#include "base/openssl_help.h"

namespace Gram {

QByteArray HmacSha512(bytes::const_span key, bytes::const_span data) {
	auto result = QByteArray(openssl::kSha512Size, 0);
	auto length = (unsigned int)openssl::kSha512Size;
	HMAC(
		EVP_sha512(),
		key.data(),
		key.size(),
		reinterpret_cast<const unsigned char*>(data.data()),
		data.size(),
		reinterpret_cast<unsigned char*>(result.data()),
		&length);
	return result;
}

} // namespace Gram
