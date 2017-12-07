/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "mtproto/rsa_public_key.h"

#include "base/openssl_help.h"
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/err.h>

using std::string;

namespace MTP {
namespace {
#if OPENSSL_VERSION_NUMBER < 0x10100000L

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
}

namespace internal {

class RSAPublicKey::Private {
public:
	Private(base::const_byte_span key)
	: _rsa(PEM_read_bio_RSAPublicKey(BIO_new_mem_buf(const_cast<gsl::byte*>(key.data()), key.size()), 0, 0, 0)) {
		if (_rsa) {
			computeFingerprint();
		}
	}
	Private(base::const_byte_span nBytes, base::const_byte_span eBytes)
	: _rsa(RSA_new()) {
		if (_rsa) {
			auto n = openssl::BigNum(nBytes).takeRaw();
			auto e = openssl::BigNum(eBytes).takeRaw();
			auto valid = (n != nullptr) && (e != nullptr);
			// We still pass both values to RSA_set0_key() so that even
			// if only one of them is valid RSA would take ownership of it.
			if (!RSA_set0_key(_rsa, n, e, nullptr) || !valid) {
				RSA_free(base::take(_rsa));
			} else {
				computeFingerprint();
			}
		}
	}
	base::byte_vector getN() const {
		Expects(isValid());
		const BIGNUM *n;
		RSA_get0_key(_rsa, &n, nullptr, nullptr);
		return toBytes(n);
	}
	base::byte_vector getE() const {
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
	base::byte_vector encrypt(base::const_byte_span data) const {
		Expects(isValid());

		constexpr auto kEncryptSize = 256;
		auto result = base::byte_vector(kEncryptSize, gsl::byte {});
		auto res = RSA_public_encrypt(kEncryptSize, reinterpret_cast<const unsigned char*>(data.data()), reinterpret_cast<unsigned char*>(result.data()), _rsa, RSA_NO_PADDING);
		if (res < 0 || res > kEncryptSize) {
			ERR_load_crypto_strings();
			LOG(("RSA Error: RSA_public_encrypt failed, key fp: %1, result: %2, error: %3").arg(getFingerPrint()).arg(res).arg(ERR_error_string(ERR_get_error(), 0)));
			return base::byte_vector();
		} else if (auto zeroBytes = kEncryptSize - res) {
			auto resultBytes = gsl::make_span(result);
			base::move_bytes(resultBytes.subspan(zeroBytes, res), resultBytes.subspan(0, res));
			base::set_bytes(resultBytes.subspan(0, zeroBytes), gsl::byte {});
		}
		return result;
	}
	base::byte_vector decrypt(base::const_byte_span data) const {
		Expects(isValid());

		constexpr auto kDecryptSize = 256;
		auto result = base::byte_vector(kDecryptSize, gsl::byte {});
		auto res = RSA_public_decrypt(kDecryptSize, reinterpret_cast<const unsigned char*>(data.data()), reinterpret_cast<unsigned char*>(result.data()), _rsa, RSA_NO_PADDING);
		if (res < 0 || res > kDecryptSize) {
			ERR_load_crypto_strings();
			LOG(("RSA Error: RSA_public_encrypt failed, key fp: %1, result: %2, error: %3").arg(getFingerPrint()).arg(res).arg(ERR_error_string(ERR_get_error(), 0)));
			return base::byte_vector();
		} else if (auto zeroBytes = kDecryptSize - res) {
			auto resultBytes = gsl::make_span(result);
			base::move_bytes(resultBytes.subspan(zeroBytes - res, res), resultBytes.subspan(0, res));
			base::set_bytes(resultBytes.subspan(0, zeroBytes - res), gsl::byte {});
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
	static base::byte_vector toBytes(const BIGNUM *number) {
		auto size = BN_num_bytes(number);
		auto result = base::byte_vector(size, gsl::byte {});
		BN_bn2bin(number, reinterpret_cast<unsigned char*>(result.data()));
		return result;
	}

	RSA *_rsa = nullptr;
	uint64 _fingerprint = 0;

};

RSAPublicKey::RSAPublicKey(base::const_byte_span key) : _private(std::make_shared<Private>(key)) {
}

RSAPublicKey::RSAPublicKey(base::const_byte_span nBytes, base::const_byte_span eBytes) : _private(std::make_shared<Private>(nBytes, eBytes)) {
}

bool RSAPublicKey::isValid() const {
	return _private && _private->isValid();
}

uint64 RSAPublicKey::getFingerPrint() const {
	Expects(isValid());
	return _private->getFingerPrint();
}

base::byte_vector RSAPublicKey::getN() const {
	Expects(isValid());
	return _private->getN();
}

base::byte_vector RSAPublicKey::getE() const {
	Expects(isValid());
	return _private->getE();
}

base::byte_vector RSAPublicKey::encrypt(base::const_byte_span data) const {
	Expects(isValid());
	return _private->encrypt(data);
}

base::byte_vector RSAPublicKey::decrypt(base::const_byte_span data) const {
	Expects(isValid());
	return _private->decrypt(data);
}

} // namespace internal
} // namespace MTP
