/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QByteArray>

#include <initializer_list>

namespace Gram {

struct HdKeyState {
	HdKeyState() = default;
	HdKeyState(const HdKeyState &other) = delete;
	HdKeyState &operator=(const HdKeyState &other) = delete;
	HdKeyState(HdKeyState &&other) = default;
	HdKeyState &operator=(HdKeyState &&other) = default;
	~HdKeyState();

	QByteArray key;
	QByteArray chainCode;

};

[[nodiscard]] HdKeyState Ed25519MasterKey(const QByteArray &seed);
[[nodiscard]] HdKeyState DeriveHardened(
	const HdKeyState &parent,
	quint32 index);
[[nodiscard]] QByteArray DeriveEd25519Path(
	const QByteArray &seed,
	std::initializer_list<quint32> path);

} // namespace Gram
