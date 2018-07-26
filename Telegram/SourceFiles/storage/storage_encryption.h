/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/bytes.h"

namespace Storage {

constexpr auto kSaltSize = size_type(64);

class CtrState {
public:
	static constexpr auto kBlockSize = size_type(16);
	static constexpr auto kKeySize = size_type(32);
	static constexpr auto kIvSize = kBlockSize;

	CtrState(bytes::const_span key, bytes::const_span iv);

	void encrypt(bytes::span data, index_type offset);
	void decrypt(bytes::span data, index_type offset);

private:
	template <typename Method>
	void process(bytes::span data, index_type offset, Method method);

	static constexpr auto EcountSize = kBlockSize;

	bytes::array<kKeySize> _key;
	bytes::array<kIvSize> _iv;

};

class EncryptionKey {
public:
	static constexpr auto kSize = size_type(256);

	EncryptionKey() = default;
	explicit EncryptionKey(bytes::vector &&data);

	const bytes::vector &data() const;
	CtrState prepareCtrState(bytes::const_span salt) const;

private:
	bytes::vector _data;

};

} // namespace Storage
