/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/rsa_public_key.h"

#include "base/openssl_help.h"

extern "C" {
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/err.h>
} // extern "C"

namespace MTP {
namespace internal {
namespace {
#if OPENSSL_VERSION_NUMBER < 0x10100000L || (defined(LIBRESSL_VERSION_NUMBER) && LIBRESSL_VERSION_NUMBER < 0x2070000fL)

// This is a key setter for compatibility with OpenSSL 1.0
int RSA_set0_key(RSA *r, BIGNUM *n, BIGNUM *e, BIGNUM *d) {
	if ((r->n == nullptr && n == nullptr) || (r->e == nullptr && e == nullptr)) {
		return 0;
	}
	if (n != nullptr) {
		BN_free(r->n);
		r->n = n;
	}
	if (e != nullptr) {
		BN_free(r->e);
		r->e = e;
	}
	if (d != nullptr) {
		BN_free(r->d);
		r->d = d;
	}
	return 1;
}

// This is a key getter for compatibility with OpenSSL 1.0
void RSA_get0_key(const RSA *r, const BIGNUM **n, const BIGNUM **e, const BIGNUM **d) {
	if (n != nullptr) {
		*n = r->n;
	}
	if (e != nullptr) {
		*e = r->e;
	}
	if (d != nullptr) {
		*d = r->d;
	}
}

#endif

enum class Format {
	RSAPublicKey,
	RSA_PUBKEY,
	Unknown,
};

Format GuessFormat(bytes::const_span key) {
	const auto array = QByteArray::fromRawData(
		reinterpret_cast<const char*>(key.data()),
		key.size());
	if (array.indexOf("BEGIN RSA PUBLIC KEY") >= 0) {
		return Format::RSAPublicKey;
	} else if (array.indexOf("BEGIN PUBLIC KEY") >= 0) {
		return Format::RSA_PUBKEY;
	}
	return Format::Unknown;
}

RSA *CreateRaw(bytes::const_span key) {
	const auto format = GuessFormat(key);
	const auto bio = BIO_new_mem_buf(
		const_cast<gsl::byte*>(key.data()),
		key.size());
	switch (format) {
	case Format::RSAPublicKey:
		return PEM_read_bio_RSAPublicKey(bio, nullptr, nullptr, nullptr);
	case Format::RSA_PUBKEY:
		return PEM_read_bio_RSA_PUBKEY(bio, nullptr, nullptr, nullptr);
	}
	Unexpected("format in RSAPublicKey::Private::Create.");
}

} // namespace

class RSAPublicKey::Private {
public:
	Private(bytes::const_span key)
	: _rsa(CreateRaw(key)) {
		if (_rsa) {
			computeFingerprint();
		}
	}
	Private(bytes::const_span nBytes, bytes::const_span eBytes)
	: _rsa(RSA_new()) {
		if (_rsa) {
			const auto n = openssl::BigNum(nBytes).takeRaw();
			const auto e = openssl::BigNum(eBytes).takeRaw();
			const auto valid = (n != nullptr) && (e != nullptr);
			// We still pass both values to RSA_set0_key() so that even
			// if only one of them is valid RSA would take ownership of it.
			if (!RSA_set0_key(_rsa, n, e, nullptr) || !valid) {
				RSA_free(base::take(_rsa));
			} else {
				computeFingerprint();
			}
		}
	}
	bytes::vector getN() const {
		Expects(isValid());

		const BIGNUM *n;
		RSA_get0_key(_rsa, &n, nullptr, nullptr);
		return toBytes(n);
	}
	bytes::vector getE() const {
		Expects(isValid());

		const BIGNUM *e;
		RSA_get0_key(_rsa, nullptr, &e, nullptr);
		return toBytes(e);
	}
	uint64 getFingerPrint() const {
		return _fingerprint;
	}
	bool isValid() const {
		return _rsa != nullptr;
	}
	bytes::vector encrypt(bytes::const_span data) const {
		Expects(isValid());

		constexpr auto kEncryptSize = 256;
		auto result = bytes::vector(kEncryptSize, gsl::byte {});
		auto res = RSA_public_encrypt(kEncryptSize, reinterpret_cast<const unsigned char*>(data.data()), reinterpret_cast<unsigned char*>(result.data()), _rsa, RSA_NO_PADDING);
		if (res < 0 || res > kEncryptSize) {
			ERR_load_crypto_strings();
			LOG(("RSA Error: RSA_public_encrypt failed, key fp: %1, result: %2, error: %3").arg(getFingerPrint()).arg(res).arg(ERR_error_string(ERR_get_error(), 0)));
			return {};
		} else if (auto zeroBytes = kEncryptSize - res) {
			auto resultBytes = gsl::make_span(result);
			bytes::move(resultBytes.subspan(zeroBytes, res), resultBytes.subspan(0, res));
			bytes::set_with_const(resultBytes.subspan(0, zeroBytes), gsl::byte {});
		}
		return result;
	}
	bytes::vector decrypt(bytes::const_span data) const {
		Expects(isValid());

		constexpr auto kDecryptSize = 256;
		auto result = bytes::vector(kDecryptSize, gsl::byte {});
		auto res = RSA_public_decrypt(kDecryptSize, reinterpret_cast<const unsigned char*>(data.data()), reinterpret_cast<unsigned char*>(result.data()), _rsa, RSA_NO_PADDING);
		if (res < 0 || res > kDecryptSize) {
			ERR_load_crypto_strings();
			LOG(("RSA Error: RSA_public_encrypt failed, key fp: %1, result: %2, error: %3").arg(getFingerPrint()).arg(res).arg(ERR_error_string(ERR_get_error(), 0)));
			return {};
		} else if (auto zeroBytes = kDecryptSize - res) {
			auto resultBytes = gsl::make_span(result);
			bytes::move(resultBytes.subspan(zeroBytes - res, res), resultBytes.subspan(0, res));
			bytes::set_with_const(resultBytes.subspan(0, zeroBytes - res), gsl::byte {});
		}
		return result;
	}
	bytes::vector encryptOAEPpadding(bytes::const_span data) const {
		Expects(isValid());

		const auto resultSize = RSA_size(_rsa);
		auto result = bytes::vector(resultSize, gsl::byte{});
		const auto encryptedSize = RSA_public_encrypt(
			data.size(),
			reinterpret_cast<const unsigned char*>(data.data()),
			reinterpret_cast<unsigned char*>(result.data()),
			_rsa,
			RSA_PKCS1_OAEP_PADDING);
		if (encryptedSize != resultSize) {
			ERR_load_crypto_strings();
			LOG(("RSA Error: RSA_public_encrypt failed, "
				"key fp: %1, result: %2, error: %3"
				).arg(getFingerPrint()
				).arg(encryptedSize
				).arg(ERR_error_string(ERR_get_error(), 0)
				));
			return {};
		}
		return result;
	}
	~Private() {
		RSA_free(_rsa);
	}

private:
	void computeFingerprint() {
		Expects(isValid());

		const BIGNUM *n, *e;
		mtpBuffer string;
		RSA_get0_key(_rsa, &n, &e, nullptr);
		MTP_bytes(toBytes(n)).write(string);
		MTP_bytes(toBytes(e)).write(string);

		uchar sha1Buffer[20];
		_fingerprint = *(uint64*)(hashSha1(&string[0], string.size() * sizeof(mtpPrime), sha1Buffer) + 3);
	}
	static bytes::vector toBytes(const BIGNUM *number) {
		auto size = BN_num_bytes(number);
		auto result = bytes::vector(size, gsl::byte {});
		BN_bn2bin(number, reinterpret_cast<unsigned char*>(result.data()));
		return result;
	}

	RSA *_rsa = nullptr;
	uint64 _fingerprint = 0;

};

RSAPublicKey::RSAPublicKey(bytes::const_span key)
: _private(std::make_shared<Private>(key)) {
}

RSAPublicKey::RSAPublicKey(
	bytes::const_span nBytes,
	bytes::const_span eBytes)
: _private(std::make_shared<Private>(nBytes, eBytes)) {
}

bool RSAPublicKey::isValid() const {
	return _private && _private->isValid();
}

uint64 RSAPublicKey::getFingerPrint() const {
	Expects(isValid());
	return _private->getFingerPrint();
}

bytes::vector RSAPublicKey::getN() const {
	Expects(isValid());

	return _private->getN();
}

bytes::vector RSAPublicKey::getE() const {
	Expects(isValid());

	return _private->getE();
}

bytes::vector RSAPublicKey::encrypt(bytes::const_span data) const {
	Expects(isValid());

	return _private->encrypt(data);
}

bytes::vector RSAPublicKey::decrypt(bytes::const_span data) const {
	Expects(isValid());

	return _private->decrypt(data);
}

bytes::vector RSAPublicKey::encryptOAEPpadding(
		bytes::const_span data) const {
	return _private->encryptOAEPpadding(data);
}

} // namespace internal
} // namespace MTP
