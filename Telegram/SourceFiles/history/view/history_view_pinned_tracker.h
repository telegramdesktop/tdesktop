/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class History;

namespace Data {
enum class LoadDirection : char;
} // namespace Data

namespace HistoryView {

struct PinnedId {
	MsgId message = 0;
	int index = 0;
	int count = 1;

	bool operator<(const PinnedId &other) const {
		return std::tie(message, index, count)
			< std::tie(other.message, other.index, other.count);
	}
	bool operator==(const PinnedId &other) const {
		return std::tie(message, index, count)
			== std::tie(other.message, other.index, other.count);
	}
};

class PinnedTracker final {
public:
	explicit PinnedTracker(not_null<History*> history);
	~PinnedTracker();

	[[nodiscard]] rpl::producer<PinnedId> shownMessageId() const;
	[[nodiscard]] PinnedId currentMessageId() const;
	void trackAround(MsgId messageId);
	void reset();

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	struct Slice {
		std::vector<MsgId> ids;
		std::optional<int> fullCount;
		std::optional<int> skippedBefore;
		std::optional<int> skippedAfter;
	};
	void clear();
	void refreshViewer();
	void refreshCurrentFromSlice();

	const not_null<History*> _history;

	rpl::variable<PinnedId> _current;
	rpl::lifetime _dataLifetime;

	MsgId _aroundId = 0;
	MsgId _viewerAroundId = 0;
	Slice _slice;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView
