/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <vector>

namespace Gram::Tests {

struct Check {
	QString name;
	Fn<QString()> run;
};

[[nodiscard]] std::vector<Check> CryptoChecks();

} // namespace Gram::Tests
