/*
This file is part of AhiGram,
a fork of Telegram Desktop with additional features.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

// Use of the code is permitted as long as links to the original source are maintained.
// Author: https://github.com/DyingLay

AhiGram: Badge Info Types
*/

#pragma once

#include "ui/style/style_core_types.h"

class QString;

namespace AhiGram {
namespace Profile {
namespace Badge {

struct BadgeInfo {
	QString title;
	QString description;
	const style::icon *icon;
};

} // namespace Badge
} // namespace Profile
} // namespace AhiGram