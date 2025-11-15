/*
This file is part of AhiGram,
a fork of Telegram Desktop with additional features.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

// This code is licensed under GPLv3. Attribution to original source is appreciated.
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