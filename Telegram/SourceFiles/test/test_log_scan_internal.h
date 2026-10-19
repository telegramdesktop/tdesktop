/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "test/test_secrecy_scan.h"

namespace Test::Internal {

struct LogScanObserver {
	Fn<void(SecrecyClass)> beginSource;
	Fn<void(const QString &, bool entryStart)> line;
};

struct LogScanReading {
	std::vector<SecrecyClassReading> classes;
	int candidates = 0;
	int accepted = 0;
	int foreign = 0;
	int missing = 0;
	int unreadable = 0;
	QStringList candidateIdentities;
};

// Callbacks are synchronous; null usable skips strict parsing.
[[nodiscard]] LogScanReading ReadLogClasses(
	const SecrecyLaunchLogs &logs,
	const QString &plantedControl,
	const SecrecySecrets *usable = nullptr,
	const std::vector<SecrecyComputedField> &computedFields = {},
	LogScanObserver *observer = nullptr);

} // namespace Test::Internal
