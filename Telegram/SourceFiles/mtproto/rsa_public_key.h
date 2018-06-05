/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/bytes.h"

namespace MTP {
namespace internal {

// this class holds an RSA public key and can encrypt fixed-size messages with it
class RSAPublicKey final {
public:
	RSAPublicKey() = default;
	RSAPublicKey(bytes::const_span nBytes, bytes::const_span eBytes);
	RSAPublicKey(RSAPublicKey &&other) = default;
	RSAPublicKey(const RSAPublicKey &other) = default;
	RSAPublicKey &operator=(RSAPublicKey &&other) = default;
	RSAPublicKey &operator=(const RSAPublicKey &other) = default;

	// key in "-----BEGIN RSA PUBLIC KEY----- ..." format
	// or in "-----BEGIN PUBLIC KEY----- ..." format
	explicit RSAPublicKey(bytes::const_span key);

	bool isValid() const;
	uint64 getFingerPrint() const;
	bytes::vector getN() const;
	bytes::vector getE() const;

	// data has exactly 256 chars to be encrypted
	bytes::vector encrypt(bytes::const_span data) const;

	// data has exactly 256 chars to be decrypted
	bytes::vector decrypt(bytes::const_span data) const;

	// data has lequal than 215 chars to be decrypted
	bytes::vector encryptOAEPpadding(bytes::const_span data) const;

private:
	class Private;
	std::shared_ptr<Private> _private;

};

} // namespace internal
} // namespace MTP
