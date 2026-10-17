/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/tests/gram_tests.h"

#include <cstdio>

namespace Gram::Tests {
namespace {

[[nodiscard]] std::vector<Check> AllChecks() {
	auto result = std::vector<Check>();
	const auto append = [&](std::vector<Check> &&checks) {
		for (auto &check : checks) {
			result.push_back(std::move(check));
		}
	};
	append(ApiChecks());
	append(RatesChecks());
	append(NftChecks());
	append(StreamChecks());
	append(EmulateChecks());
	append(BocChecks());
	return result;
}

} // namespace
} // namespace Gram::Tests

int main(int argc, char *argv[]) {
	const auto checks = Gram::Tests::AllChecks();
	auto failed = 0;
	for (const auto &check : checks) {
		const auto failure = check.run();
		if (failure.isEmpty()) {
			std::printf("PASS %s\n", check.name.toUtf8().constData());
		} else {
			std::printf(
				"FAIL %s: %s\n",
				check.name.toUtf8().constData(),
				failure.toUtf8().constData());
			++failed;
		}
	}
	std::printf(
		"%d of %d checks passed.\n",
		int(checks.size()) - failed,
		int(checks.size()));
	return failed ? 1 : 0;
}
