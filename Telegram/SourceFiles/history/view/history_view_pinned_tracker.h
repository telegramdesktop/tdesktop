/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class History;

namespace Data {
class PinnedMessages;
enum class LoadDirection : char;
} // namespace Data

namespace HistoryView {

enum class PinnedIdType {
	First,
	Middle,
	Last,
};
struct PinnedId {
	MsgId message = 0;
	PinnedIdType type = PinnedIdType::Middle;

	bool operator<(const PinnedId &other) const {
		return std::tie(message, type) < std::tie(other.message, other.type);
	}
	bool operator==(const PinnedId &other) const {
		return std::tie(message, type) == std::tie(other.message, other.type);
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

private:
	void refreshData();
	void setupViewer(not_null<Data::PinnedMessages*> data);
	void load(Data::LoadDirection direction, MsgId id);
	void apply(
		Data::LoadDirection direction,
		MsgId aroundId,
		const MTPmessages_Messages &result);

	const not_null<History*> _history;

	base::weak_ptr<Data::PinnedMessages> _data;
	rpl::variable<PinnedId> _current;
	rpl::lifetime _dataLifetime;

	MsgId _aroundId = 0;
	MsgId _beforeId = 0;
	MsgId _afterId = 0;
	MsgId _beforeRequestId = 0;
	MsgId _afterRequestId = 0;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView
