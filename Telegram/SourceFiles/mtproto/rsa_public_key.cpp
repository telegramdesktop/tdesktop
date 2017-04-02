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

namespace MTP {
namespace internal {

struct RSAPublicKey::Impl {
	Impl(const char *key) : rsa(PEM_read_bio_RSAPublicKey(BIO_new_mem_buf(const_cast<char*>(key), -1), 0, 0, 0)) {
	}
	~Impl() {
		RSA_free(rsa);
	}
	RSA *rsa;
	uint64 fp = 0;
};

RSAPublicKey::RSAPublicKey(const char *key) : impl_(new Impl(key)) {
	if (!impl_->rsa) return;

	int nBytes = BN_num_bytes(impl_->rsa->n);
	int eBytes = BN_num_bytes(impl_->rsa->e);
	std::string nStr(nBytes, 0), eStr(eBytes, 0);
	BN_bn2bin(impl_->rsa->n, (uchar*)&nStr[0]);
	BN_bn2bin(impl_->rsa->e, (uchar*)&eStr[0]);

	mtpBuffer tmp;
	MTP_string(nStr).write(tmp);
	MTP_string(eStr).write(tmp);

	uchar sha1Buffer[20];
	impl_->fp = *(uint64*)(hashSha1(&tmp[0], tmp.size() * sizeof(mtpPrime), sha1Buffer) + 3);
}

uint64 RSAPublicKey::getFingerPrint() const {
	return impl_->fp;
}

bool RSAPublicKey::isValid() const {
	return impl_->rsa != nullptr;
}

bool RSAPublicKey::encrypt(const void *data, std::string &result) const {
	Expects(isValid());

	result.resize(256);
	int res = RSA_public_encrypt(256, reinterpret_cast<const unsigned char*>(data), reinterpret_cast<uchar*>(&result[0]), impl_->rsa, RSA_NO_PADDING);
	if (res != 256) {
		ERR_load_crypto_strings();
		LOG(("RSA Error: RSA_public_encrypt failed, key fp: %1, result: %2, error: %3").arg(getFingerPrint()).arg(res).arg(ERR_error_string(ERR_get_error(), 0)));
		return false;
	}
	return true;
}

} // namespace internal
} // namespace MTP
