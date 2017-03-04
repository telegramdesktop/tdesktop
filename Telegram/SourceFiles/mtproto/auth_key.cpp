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
#include "mtproto/auth_key.h"

#include <openssl/aes.h>

namespace MTP {

void aesIgeEncrypt(const void *src, void *dst, uint32 len, const void *key, const void *iv) {
	uchar aes_key[32], aes_iv[32];
	memcpy(aes_key, key, 32);
	memcpy(aes_iv, iv, 32);

	AES_KEY aes;
	AES_set_encrypt_key(aes_key, 256, &aes);
	AES_ige_encrypt(static_cast<const uchar*>(src), static_cast<uchar*>(dst), len, &aes, aes_iv, AES_ENCRYPT);
}

void aesIgeDecrypt(const void *src, void *dst, uint32 len, const void *key, const void *iv) {
	uchar aes_key[32], aes_iv[32];
	memcpy(aes_key, key, 32);
	memcpy(aes_iv, iv, 32);

	AES_KEY aes;
	AES_set_decrypt_key(aes_key, 256, &aes);
	AES_ige_encrypt(static_cast<const uchar*>(src), static_cast<uchar*>(dst), len, &aes, aes_iv, AES_DECRYPT);
}

void aesCtrEncrypt(void *data, uint32 len, const void *key, CTRState *state) {
	AES_KEY aes;
	AES_set_encrypt_key(static_cast<const uchar*>(key), 256, &aes);

	static_assert(CTRState::IvecSize == AES_BLOCK_SIZE, "Wrong size of ctr ivec!");
	static_assert(CTRState::EcountSize == AES_BLOCK_SIZE, "Wrong size of ctr ecount!");

	AES_ctr128_encrypt(static_cast<const uchar*>(data), static_cast<uchar*>(data), len, &aes, state->ivec, state->ecount, &state->num);
}

} // namespace MTP
