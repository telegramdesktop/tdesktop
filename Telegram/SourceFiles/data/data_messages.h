/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {

enum class LoadDirection : char {
	Around,
	Before,
	After,
};

struct MessagePosition {
	FullMsgId fullId;
	TimeId date = 0;

	explicit operator bool() const {
		return (fullId.msg != 0);
	}

	inline constexpr bool operator<(const MessagePosition &other) const {
		if (date < other.date) {
			return true;
		} else if (other.date < date) {
			return false;
		}
		return (fullId < other.fullId);
	}
	inline constexpr bool operator>(const MessagePosition &other) const {
		return other < *this;
	}
	inline constexpr bool operator<=(const MessagePosition &other) const {
		return !(other < *this);
	}
	inline constexpr bool operator>=(const MessagePosition &other) const {
		return !(*this < other);
	}
	inline constexpr bool operator==(const MessagePosition &other) const {
		return (date == other.date)
			&& (fullId == other.fullId);
	}
	inline constexpr bool operator!=(const MessagePosition &other) const {
		return !(*this == other);
	}
};

constexpr auto MinDate = TimeId(0);
constexpr auto MaxDate = std::numeric_limits<TimeId>::max();
constexpr auto MinMessagePosition = MessagePosition{
	.fullId = FullMsgId(PeerId(), 1),
	.date = MinDate,
};
constexpr auto MaxMessagePosition = MessagePosition{
	.fullId = FullMsgId(PeerId(), ServerMaxMsgId - 1),
	.date = MaxDate,
};
constexpr auto UnreadMessagePosition = MessagePosition{
	.fullId = FullMsgId(PeerId(), ShowAtUnreadMsgId),
	.date = MinDate,
};

struct MessagesSlice {
	std::vector<FullMsgId> ids;
	FullMsgId nearestToAround;
	std::optional<int> skippedBefore;
	std::optional<int> skippedAfter;
	std::optional<int> fullCount;
};

} // namespace Data
