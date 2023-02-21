/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace PowerSaving {

enum Flag : uint32 {
	kAnimations = (1U << 0),
	kStickersPanel = (1U << 1),
	kStickersChat = (1U << 2),
	kEmojiPanel = (1U << 3),
	kEmojiReactions = (1U << 4),
	kEmojiChat = (1U << 5),
	kChatBackground = (1U << 6),
	kChatSpoiler = (1U << 7),
	kCalls = (1U << 8),

	kAll = (1U << 9) - 1,
};
inline constexpr bool is_flag_type(Flag) { return true; }
using Flags = base::flags<Flag>;

[[nodiscard]] Flags Current();
[[nodiscard]] rpl::producer<Flags> Changes();
[[nodiscard]] rpl::producer<Flags> Value();
[[nodiscard]] rpl::producer<bool> Value(Flag flag);
[[nodiscard]] void Set(Flags flags);

[[nodiscard]] inline bool On(Flag flag) {
	return Current() & flag;
}

} // namespace PowerSaving
