/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text_entity.h"

namespace SendMenu {

enum class Type : uchar {
	Disabled,
	SilentOnly,
	Scheduled,
	ScheduledToUser, // For "Send when online".
	Reminder,
	EditCommentPrice,
};

enum class SpoilerState : uchar {
	None,
	Enabled,
	Possible,
};

enum class CaptionState : uchar {
	None,
	Below,
	Above,
};

enum class PhotoQualityState : uchar {
	None,
	Standard,
	High,
};

struct Details {
	Type type = Type::Disabled;
	SpoilerState spoiler = SpoilerState::None;
	CaptionState caption = CaptionState::None;
	PhotoQualityState photoQuality = PhotoQualityState::None;
	TextWithTags commentPreview;
	QString commentStreamerName;
	std::optional<uint64> price;
	std::optional<uint64> commentPriceMin;
	bool effectAllowed = false;
};

} // namespace SendMenu
