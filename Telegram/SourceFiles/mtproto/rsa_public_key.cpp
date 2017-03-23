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

#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/err.h>

using std::string;

namespace MTP {
namespace internal {

class RSAPublicKey::Private {
public:
	Private(base::const_byte_span key) : _rsa(PEM_read_bio_RSAPublicKey(BIO_new_mem_buf(const_cast<gsl::byte*>(key.data()), key.size()), 0, 0, 0)) {
		if (_rsa) {
			computeFingerprint();
		}
	}
	Private(const QByteArray &n, const QByteArray &e) : _rsa(RSA_new()) {
		if (_rsa) {
			_rsa->n = BN_bin2bn((const uchar*)n.data(), n.size(), _rsa->n);
			_rsa->e = BN_bin2bn((const uchar*)e.data(), e.size(), _rsa->e);
			if (!_rsa->n || !_rsa->e) {
				RSA_free(base::take(_rsa));
			} else {
				computeFingerprint();
			}
		}
	}
	QByteArray getN() const {
		Expects(isValid());
		return toBytes(_rsa->n);
	}
	QByteArray getE() const {
		Expects(isValid());
		return toBytes(_rsa->e);
	}
	uint64 getFingerPrint() const {
		return _fingerprint;
	}
	bool isValid() const {
		return _rsa != nullptr;
	}
	bool encrypt(const void *data, string &result) const {
		Expects(isValid());

		result.resize(256);
		auto res = RSA_public_encrypt(256, reinterpret_cast<const unsigned char*>(data), reinterpret_cast<uchar*>(&result[0]), _rsa, RSA_NO_PADDING);
		if (res != 256) {
			ERR_load_crypto_strings();
			LOG(("RSA Error: RSA_public_encrypt failed, key fp: %1, result: %2, error: %3").arg(getFingerPrint()).arg(res).arg(ERR_error_string(ERR_get_error(), 0)));
			return false;
		}
		return true;
	}
	~Private() {
		RSA_free(_rsa);
	}

private:
	void computeFingerprint() {
		Expects(isValid());

		mtpBuffer string;
		MTP_bytes(toBytes(_rsa->n)).write(string);
		MTP_bytes(toBytes(_rsa->e)).write(string);

		uchar sha1Buffer[20];
		_fingerprint = *(uint64*)(hashSha1(&string[0], string.size() * sizeof(mtpPrime), sha1Buffer) + 3);
	}
	static QByteArray toBytes(BIGNUM *number) {
		auto size = static_cast<int>(BN_num_bytes(number));
		auto result = QByteArray(size, 0);
		BN_bn2bin(number, reinterpret_cast<uchar*>(result.data()));
		return result;
	}

	RSA *_rsa = nullptr;
	uint64 _fingerprint = 0;

};

RSAPublicKey::RSAPublicKey(base::const_byte_span key) : _private(std::make_shared<Private>(key)) {
}

RSAPublicKey::RSAPublicKey(const QByteArray &n, const QByteArray &e) : _private(std::make_shared<Private>(n, e)) {
}

bool RSAPublicKey::isValid() const {
	return _private && _private->isValid();
}

uint64 RSAPublicKey::getFingerPrint() const {
	Expects(isValid());
	return _private->getFingerPrint();
}

QByteArray RSAPublicKey::getN() const {
	Expects(isValid());
	return _private->getN();
}

QByteArray RSAPublicKey::getE() const {
	Expects(isValid());
	return _private->getE();
}

bool RSAPublicKey::encrypt(const void *data, string &result) const {
	Expects(isValid());
	return _private->encrypt(data, result);
}

} // namespace internal
} // namespace MTP
