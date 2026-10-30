/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>

namespace Glass {

enum class Quality {
	Off = 0,
	Solid = 1,
};

enum class Weight {
	Tinted = 0,
	Opaque = 1,
	HighContrast = 2,
};

struct Policy {
	Quality quality = Quality::Off;
	Weight weight = Weight::Tinted;
	bool motionReduced = false;
};

[[nodiscard]] bool Supported();
[[nodiscard]] Policy Current();
[[nodiscard]] rpl::producer<> Changes();

} // namespace Glass
