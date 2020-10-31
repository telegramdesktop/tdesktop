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

struct MessagesRange {
	MessagePosition from;
	MessagePosition till;

	inline constexpr bool operator==(const MessagesRange &other) const {
		return (from == other.from)
			&& (till == other.till);
	}
	inline constexpr bool operator!=(const MessagesRange &other) const {
		return !(*this == other);
	}
};

constexpr auto MinDate = TimeId(0);
constexpr auto MaxDate = std::numeric_limits<TimeId>::max();
constexpr auto MinMessagePosition = MessagePosition{
	.fullId = FullMsgId(NoChannel, 1),
	.date = MinDate,
};
constexpr auto MaxMessagePosition = MessagePosition{
	.fullId = FullMsgId(NoChannel, ServerMaxMsgId - 1),
	.date = MaxDate,
};
constexpr auto FullMessagesRange = MessagesRange{
	.from = MinMessagePosition,
	.till = MaxMessagePosition,
};
constexpr auto UnreadMessagePosition = MessagePosition{
	.fullId = FullMsgId(NoChannel, ShowAtUnreadMsgId),
	.date = MinDate,
};

struct MessagesSlice {
	std::vector<FullMsgId> ids;
	FullMsgId nearestToAround;
	std::optional<int> skippedBefore;
	std::optional<int> skippedAfter;
	std::optional<int> fullCount;
};

struct MessagesQuery {
	MessagePosition aroundId;
	int limitBefore = 0;
	int limitAfter = 0;
};

struct MessagesResult {
	std::optional<int> count;
	std::optional<int> skippedBefore;
	std::optional<int> skippedAfter;
	base::flat_set<MessagePosition> messageIds;
};

struct MessagesSliceUpdate {
	const base::flat_set<MessagePosition> *messages = nullptr;
	MessagesRange range;
	std::optional<int> count;
};

class MessagesList {
public:
	void addOne(MessagePosition messageId);
	void addNew(MessagePosition messageId);
	void addSlice(
		std::vector<MessagePosition> &&messageIds,
		MessagesRange noSkipRange,
		std::optional<int> count);
	void removeOne(MessagePosition messageId);
	void removeAll(ChannelId channelId);
	void removeLessThan(MessagePosition messageId);
	void invalidate();
	void invalidateBottom();
	[[nodiscard]] rpl::producer<MessagesResult> query(
		MessagesQuery &&query) const;
	[[nodiscard]] rpl::producer<MessagesSliceUpdate> sliceUpdated() const;

	[[nodiscard]] MessagesResult snapshot(MessagesQuery &&query) const;
	[[nodiscard]] rpl::producer<MessagesResult> viewer(
		MessagesQuery &&query) const;

	[[nodiscard]] bool empty() const;

private:
	struct Slice {
		Slice(
			base::flat_set<MessagePosition> &&messages,
			MessagesRange range);

		template <typename Range>
		void merge(
			const Range &moreMessages,
			MessagesRange moreNoSkipRange);

		base::flat_set<MessagePosition> messages;
		MessagesRange range;

		inline bool operator<(const Slice &other) const {
			return range.from < other.range.from;
		}

	};

	template <typename Range>
	int uniteAndAdd(
		MessagesSliceUpdate &update,
		base::flat_set<Slice>::iterator uniteFrom,
		base::flat_set<Slice>::iterator uniteTill,
		const Range &messages,
		MessagesRange noSkipRange);
	template <typename Range>
	int addRangeItemsAndCountNew(
		MessagesSliceUpdate &update,
		const Range &messages,
		MessagesRange noSkipRange);
	template <typename Range>
	void addRange(
		const Range &messages,
		MessagesRange noSkipRange,
		std::optional<int> count,
		bool incrementCount = false);

	MessagesResult queryFromSlice(
		const MessagesQuery &query,
		const Slice &slice) const;
	MessagesResult queryCurrent(const MessagesQuery &query) const;

	std::optional<int> _count;
	base::flat_set<Slice> _slices;

	rpl::event_stream<MessagesSliceUpdate> _sliceUpdated;

};

class MessagesSliceBuilder {
public:
	using Key = MessagePosition;

	MessagesSliceBuilder(Key key, int limitBefore, int limitAfter);

	bool applyInitial(const MessagesResult &result);
	bool applyUpdate(const MessagesSliceUpdate &update);
	bool removeOne(MessagePosition messageId);
	bool removeFromChannel(ChannelId channelId);
	bool removeAll();
	bool invalidated();
	bool bottomInvalidated();

	void checkInsufficient();
	struct AroundData {
		MessagePosition aroundId;
		LoadDirection direction = LoadDirection::Around;

		inline bool operator<(const AroundData &other) const {
			return (aroundId < other.aroundId)
				|| ((aroundId == other.aroundId)
					&& (direction < other.direction));
		}
	};
	auto insufficientAround() const {
		return _insufficientAround.events();
	}

	MessagesSlice snapshot() const;

private:
	enum class RequestDirection {
		Before,
		After,
	};
	void requestMessages(RequestDirection direction);
	void requestMessagesCount();
	void fillSkippedAndSliceToLimits();
	void sliceToLimits();

	void mergeSliceData(
		std::optional<int> count,
		const base::flat_set<MessagePosition> &messageIds,
		std::optional<int> skippedBefore = std::nullopt,
		std::optional<int> skippedAfter = std::nullopt);

	MessagePosition _key;
	base::flat_set<MessagePosition> _ids;
	MessagesRange _range;
	std::optional<int> _fullCount;
	std::optional<int> _skippedBefore;
	std::optional<int> _skippedAfter;
	int _limitBefore = 0;
	int _limitAfter = 0;

	rpl::event_stream<AroundData> _insufficientAround;

};

} // namespace Data
