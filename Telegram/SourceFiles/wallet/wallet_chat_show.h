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

namespace Main {
class SessionShow;
} // namespace Main

namespace Wallet {

// WHY: a gift preview must stay inside the wallet panel while a chat link
// needs a Telegram window, so one show over the panel's own either resolves
// a window for what asks or refuses, and boxes and toasts stay put.
[[nodiscard]] std::shared_ptr<ChatHelpers::Show> MakeChatShow(
	std::shared_ptr<Main::SessionShow> show,
	bool resolveWindows);

} // namespace Wallet
