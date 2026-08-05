/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text_entity.h"

namespace style {
struct EmojiPan;
} // namespace style

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

enum class CoverState : uchar {
	None,
	Add,
	Has,
};

struct Details {
	Type type = Type::Disabled;
	uint64 barePeerId = 0;
	int64 bareTopicRootId = 0;
	SpoilerState spoiler = SpoilerState::None;
	CaptionState caption = CaptionState::None;
	PhotoQualityState photoQuality = PhotoQualityState::None;
	CoverState cover = CoverState::None;
	TextWithTags commentPreview;
	QString commentStreamerName;
	std::optional<uint64> price;
	std::optional<uint64> commentPriceMin;
	const style::EmojiPan *effectsPan = nullptr;
	bool effectAllowed = false;
};

} // namespace SendMenu
