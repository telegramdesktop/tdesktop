/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QByteArray>

#include <vector>

namespace Gram::Tests {

struct Check {
	QString name;
	Fn<QString()> run;
};

[[nodiscard]] inline QString CompareHex(
		const QByteArray &got,
		const QByteArray &expectedHex) {
	if (got == QByteArray::fromHex(expectedHex)) {
		return QString();
	}
	return u"got "_q
		+ QString::fromLatin1(got.toHex())
		+ u", expected "_q
		+ QString::fromLatin1(expectedHex);
}

[[nodiscard]] std::vector<Check> CryptoChecks();
[[nodiscard]] std::vector<Check> KeyChecks();
[[nodiscard]] std::vector<Check> MnemonicChecks();
[[nodiscard]] std::vector<Check> TonChecks();
[[nodiscard]] std::vector<Check> WalletChecks();

} // namespace Gram::Tests
