/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/iv_rich_page.h"

#include <optional>

namespace Main {
class Session;
} // namespace Main

namespace Iv {

enum class SerializeInputRichMessageMode : uchar {
	Draft,
	FinalSubmit,
};

enum class SerializeInputRichMessageStatus : uchar {
	Success,
	EmptyContent,
	Failed,
};

struct SerializeInputRichMessageResult {
	SerializeInputRichMessageStatus status
		= SerializeInputRichMessageStatus::Failed;
	std::optional<MTPInputRichMessage> value;
};

[[nodiscard]] SerializeInputRichMessageResult SerializeInputRichMessage(
	not_null<Main::Session*> session,
	const RichPage &page,
	SerializeInputRichMessageMode mode);

} // namespace Iv
