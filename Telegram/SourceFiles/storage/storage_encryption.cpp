/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/storage_encryption.h"

#include "base/openssl_help.h"

namespace Storage {

CtrState::CtrState(bytes::const_span key, bytes::const_span iv) {
	Expects(key.size() == _key.size());
	Expects(iv.size() == _iv.size());

	bytes::copy(_key, key);
	bytes::copy(_iv, iv);
}

template <typename Method>
void CtrState::process(bytes::span data, int64 offset, Method method) {
	Expects((data.size() % kBlockSize) == 0);
	Expects((offset % kBlockSize) == 0);

	AES_KEY aes;
	AES_set_encrypt_key(
		reinterpret_cast<const uchar*>(_key.data()),
		_key.size() * CHAR_BIT,
		&aes);

	unsigned char ecountBuf[kBlockSize] = { 0 };
	unsigned int offsetInBlock = 0;
	const auto blockIndex = offset / kBlockSize;
	auto iv = incrementedIv(blockIndex);

	CRYPTO_ctr128_encrypt(
		reinterpret_cast<const uchar*>(data.data()),
		reinterpret_cast<uchar*>(data.data()),
		data.size(),
		&aes,
		reinterpret_cast<unsigned char*>(iv.data()),
		ecountBuf,
		&offsetInBlock,
		(block128_f)method);
}

auto CtrState::incrementedIv(int64 blockIndex)
-> bytes::array<kIvSize> {
	Expects(blockIndex >= 0);

	if (!blockIndex) {
		return _iv;
	}
	auto result = _iv;
	auto digits = kIvSize;
	auto increment = uint64(blockIndex);
	do {
		--digits;
		increment += static_cast<uint64>(result[digits]);
		result[digits] = static_cast<bytes::type>(increment & 0xFFULL);
		increment >>= 8;
	} while (digits != 0 && increment != 0);
	return result;
}

void CtrState::encrypt(bytes::span data, int64 offset) {
	return process(data, offset, AES_encrypt);
}

void CtrState::decrypt(bytes::span data, int64 offset) {
	return process(data, offset, AES_encrypt);
}

EncryptionKey::EncryptionKey(bytes::vector &&data)
: _data(std::move(data)) {
	Expects(_data.size() == kSize);
}

bool EncryptionKey::empty() const {
	return _data.empty();
}

EncryptionKey::operator bool() const {
	return !empty();
}

const bytes::vector &EncryptionKey::data() const {
	return _data;
}

CtrState EncryptionKey::prepareCtrState(bytes::const_span salt) const {
	Expects(salt.size() == kSaltSize);

	const auto data = bytes::make_span(_data);
	const auto key = openssl::Sha256(
		data.subspan(0, kSize / 2),
		salt.subspan(0, kSaltSize / 2));
	const auto iv = openssl::Sha256(
		data.subspan(kSize / 2),
		salt.subspan(kSaltSize / 2));

	return CtrState(
		key,
		bytes::make_span(iv).subspan(0, CtrState::kIvSize));
}

} // namespace Storage
