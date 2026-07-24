/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QByteArray>

namespace Gram {

struct KeyPair {
	KeyPair() = default;
	KeyPair(const KeyPair &other) = delete;
	KeyPair &operator=(const KeyPair &other) = delete;
	KeyPair(KeyPair &&other) = default;
	KeyPair &operator=(KeyPair &&other) = default;
	~KeyPair();

	QByteArray publicKey;
	QByteArray secretKey;

};

[[nodiscard]] KeyPair KeyPairFromSeed(const QByteArray &seed32);
[[nodiscard]] QByteArray Sign(
	const QByteArray &data,
	const QByteArray &secretKey64);
[[nodiscard]] bool Verify(
	const QByteArray &data,
	const QByteArray &signature,
	const QByteArray &publicKey32);
[[nodiscard]] QByteArray FakeSign(const QByteArray &data);

} // namespace Gram
