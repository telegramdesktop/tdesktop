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
	constexpr MessagePosition() = default;
	constexpr MessagePosition(TimeId date, FullMsgId fullId)
	: fullId(fullId)
	, date(date) {
	}

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

	FullMsgId fullId;
	TimeId date = 0;

};

struct MessagesRange {
	constexpr MessagesRange() = default;
	constexpr MessagesRange(MessagePosition from, MessagePosition till)
	: from(from)
	, till(till) {
	}

	inline constexpr bool operator==(const MessagesRange &other) const {
		return (from == other.from)
			&& (till == other.till);
	}
	inline constexpr bool operator!=(const MessagesRange &other) const {
		return !(*this == other);
	}

	MessagePosition from;
	MessagePosition till;

};

constexpr auto MinDate = TimeId(0);
constexpr auto MaxDate = std::numeric_limits<TimeId>::max();
constexpr auto MinMessagePosition = MessagePosition(
	MinDate,
	FullMsgId(NoChannel, 1));
constexpr auto MaxMessagePosition = MessagePosition(
	MaxDate,
	FullMsgId(NoChannel, ServerMaxMsgId - 1));
constexpr auto FullMessagesRange = MessagesRange(
	MinMessagePosition,
	MaxMessagePosition);
constexpr auto UnreadMessagePosition = MessagePosition(
	MinDate,
	FullMsgId(NoChannel, ShowAtUnreadMsgId));

struct MessagesSlice {
	std::vector<FullMsgId> ids;
	base::optional<int> skippedBefore;
	base::optional<int> skippedAfter;
	base::optional<int> fullCount;

};

struct MessagesQuery {
	MessagesQuery(
		MessagePosition aroundId,
		int limitBefore,
		int limitAfter)
	: aroundId(aroundId)
	, limitBefore(limitBefore)
	, limitAfter(limitAfter) {
	}

	MessagePosition aroundId;
	int limitBefore = 0;
	int limitAfter = 0;

};

struct MessagesResult {
	base::optional<int> count;
	base::optional<int> skippedBefore;
	base::optional<int> skippedAfter;
	base::flat_set<MessagePosition> messageIds;
};

struct MessagesSliceUpdate {
	const base::flat_set<MessagePosition> *messages = nullptr;
	MessagesRange range;
	base::optional<int> count;
};

class MessagesList {
public:
	void addNew(MessagePosition messageId);
	void addSlice(
		std::vector<MessagePosition> &&messageIds,
		MessagesRange noSkipRange,
		base::optional<int> count);
	void removeOne(MessagePosition messageId);
	void removeAll(ChannelId channelId);
	void invalidate();
	void invalidateBottom();
	rpl::producer<MessagesResult> query(MessagesQuery &&query) const;
	rpl::producer<MessagesSliceUpdate> sliceUpdated() const;

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
		base::optional<int> count,
		bool incrementCount = false);

	MessagesResult queryFromSlice(
		const MessagesQuery &query,
		const Slice &slice) const;

	base::optional<int> _count;
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
		base::optional<int> count,
		const base::flat_set<MessagePosition> &messageIds,
		base::optional<int> skippedBefore = base::none,
		base::optional<int> skippedAfter = base::none);

	MessagePosition _key;
	base::flat_set<MessagePosition> _ids;
	MessagesRange _range;
	base::optional<int> _fullCount;
	base::optional<int> _skippedBefore;
	base::optional<int> _skippedAfter;
	int _limitBefore = 0;
	int _limitAfter = 0;

	rpl::event_stream<AroundData> _insufficientAround;

};

} // namespace Data
