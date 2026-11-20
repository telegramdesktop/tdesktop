/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QString>

#include <cstdio>
#include <initializer_list>
#include <vector>

namespace Test::NamedChecks {

struct Check {
	const char *name = nullptr;
	QString (*run)() = nullptr;
};

[[nodiscard]] inline QString FirstFailure(
		std::initializer_list<QString> results) {
	for (const auto &result : results) {
		if (!result.isEmpty()) {
			return result;
		}
	}
	return QString();
}

[[nodiscard]] inline int Run(const std::vector<Check> &checks) {
	auto failed = 0;
	for (const auto &check : checks) {
		const auto failure = check.run();
		if (failure.isEmpty()) {
			std::printf("PASS %s\n", check.name);
		} else {
			std::printf(
				"FAIL %s: %s\n",
				check.name,
				failure.toUtf8().constData());
			++failed;
		}
	}
	const auto total = int(checks.size());
	std::printf("%d of %d checks passed.\n", total - failed, total);
	return failed ? 1 : 0;
}

} // namespace Test::NamedChecks
