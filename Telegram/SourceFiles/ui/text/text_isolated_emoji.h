/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
namespace Text {

inline constexpr auto kIsolatedEmojiLimit = 3;

struct IsolatedEmoji {
	using Items = std::array<EmojiPtr, kIsolatedEmojiLimit>;
	Items items = { { nullptr } };

	[[nodiscard]] bool empty() const {
		return items[0] == nullptr;
	}
	[[nodiscard]] explicit operator bool() const {
		return !empty();
	}
	[[nodiscard]] bool operator<(const IsolatedEmoji &other) const {
		return items < other.items;
	}
	[[nodiscard]] bool operator==(const IsolatedEmoji &other) const {
		return items == other.items;
	}
	[[nodiscard]] bool operator>(const IsolatedEmoji &other) const {
		return other < *this;
	}
	[[nodiscard]] bool operator<=(const IsolatedEmoji &other) const {
		return !(other < *this);
	}
	[[nodiscard]] bool operator>=(const IsolatedEmoji &other) const {
		return !(*this < other);
	}
	[[nodiscard]] bool operator!=(const IsolatedEmoji &other) const {
		return !(*this == other);
	}
};

} // namespace Text
} // namespace Ui
