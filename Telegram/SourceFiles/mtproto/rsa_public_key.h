/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace MTP {
namespace internal {

// this class holds an RSA public key and can encrypt fixed-size messages with it
class RSAPublicKey final {
public:
	RSAPublicKey() = default;
	RSAPublicKey(base::const_byte_span nBytes, base::const_byte_span eBytes);
	RSAPublicKey(RSAPublicKey &&other) = default;
	RSAPublicKey(const RSAPublicKey &other) = default;
	RSAPublicKey &operator=(RSAPublicKey &&other) = default;
	RSAPublicKey &operator=(const RSAPublicKey &other) = default;

	// key in "-----BEGIN RSA PUBLIC KEY----- ..." format
	// or in "-----BEGIN PUBLIC KEY----- ..." format
	explicit RSAPublicKey(base::const_byte_span key);

	bool isValid() const;
	uint64 getFingerPrint() const;
	base::byte_vector getN() const;
	base::byte_vector getE() const;

	// data has exactly 256 chars to be encrypted
	base::byte_vector encrypt(base::const_byte_span data) const;

	// data has exactly 256 chars to be decrypted
	base::byte_vector decrypt(base::const_byte_span data) const;

private:
	class Private;
	std::shared_ptr<Private> _private;

};

} // namespace internal
} // namespace MTP
