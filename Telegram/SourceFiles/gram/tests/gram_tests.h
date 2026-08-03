/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QByteArray>
#include <QtCore/QFile>

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

[[nodiscard]] inline QByteArray ReadFixture(const QString &name) {
	auto file = QFile(
		QString::fromUtf8(GRAM_TEST_FIXTURES_PATH) + u"/"_q + name);
	if (!file.open(QIODevice::ReadOnly)) {
		return QByteArray();
	}
	return file.readAll().trimmed();
}

[[nodiscard]] std::vector<Check> CryptoChecks();
[[nodiscard]] std::vector<Check> KeyChecks();
[[nodiscard]] std::vector<Check> MnemonicChecks();
[[nodiscard]] std::vector<Check> TonChecks();
[[nodiscard]] std::vector<Check> LinkChecks();
[[nodiscard]] std::vector<Check> WalletChecks();
[[nodiscard]] std::vector<Check> ApiChecks();
[[nodiscard]] std::vector<Check> RatesChecks();

} // namespace Gram::Tests
