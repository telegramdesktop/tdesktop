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

class PinnedTracker final {
public:
	explicit PinnedTracker(not_null<History*> history);
	~PinnedTracker();

	[[nodiscard]] rpl::producer<MsgId> shownMessageId() const;
	void trackAround(MsgId messageId);

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
	rpl::variable<MsgId> _current = MsgId();
	rpl::lifetime _dataLifetime;

	MsgId _aroundId = 0;
	MsgId _beforeId = 0;
	MsgId _afterId = 0;
	MsgId _beforeRequestId = 0;
	MsgId _afterRequestId = 0;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView
