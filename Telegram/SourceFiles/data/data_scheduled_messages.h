/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/history_item.h"

class History;

namespace Main {
class Session;
} // namespace Main

namespace Data {

class Session;
struct MessagesSlice;

class ScheduledMessages final {
public:
	explicit ScheduledMessages(not_null<Session*> owner);
	ScheduledMessages(const ScheduledMessages &other) = delete;
	ScheduledMessages &operator=(const ScheduledMessages &other) = delete;
	~ScheduledMessages();

	[[nodiscard]] MsgId lookupId(not_null<HistoryItem*> item) const;
	[[nodiscard]] int count(not_null<History*> history) const;

	void apply(const MTPDupdateNewScheduledMessage &update);
	void apply(const MTPDupdateDeleteScheduledMessages &update);

	[[nodiscard]] rpl::producer<> updates(not_null<History*> history);
	[[nodiscard]] Data::MessagesSlice list(not_null<History*> history);

private:
	using OwnedItem = std::unique_ptr<HistoryItem, HistoryItem::Destroyer>;
	struct List {
		std::vector<OwnedItem> items;
		base::flat_map<MsgId, not_null<HistoryItem*>> itemById;
		base::flat_map<not_null<HistoryItem*>, MsgId> idByItem;
	};
	struct Request {
		int32 hash = 0;
		mtpRequestId requestId = 0;
	};

	void request(not_null<History*> history);
	void parse(
		not_null<History*> history,
		const MTPmessages_Messages &list);
	void append(
		not_null<History*> history,
		List &list,
		const MTPMessage &message);
	void remove(not_null<const HistoryItem*> item);
	[[nodiscard]] int32 countListHash(const List &list) const;

	const not_null<Main::Session*> _session;

	base::flat_map<not_null<History*>, List> _data;
	base::flat_map<not_null<History*>, Request> _requests;
	rpl::event_stream<not_null<History*>> _updates;

	rpl::lifetime _lifetime;

};

} // namespace Data
