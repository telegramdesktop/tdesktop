/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Media::Stories {

void SetupStealthMode(std::shared_ptr<ChatHelpers::Show> show);

[[nodiscard]] QString TimeLeftText(int left);

} // namespace Media::Stories
