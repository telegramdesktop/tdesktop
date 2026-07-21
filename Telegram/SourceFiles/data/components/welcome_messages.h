/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/history_item.h"
#include "base/timer.h"

class History;
class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Data {

struct MessagesSlice;

[[nodiscard]] bool IsWelcomeMsgId(MsgId id);
[[nodiscard]] int WelcomeMessagesLimit(not_null<Main::Session*> session);
[[nodiscard]] rpl::producer<int> WelcomeMessagesLimitValue(
	not_null<Main::Session*> session);

class WelcomeMessages final {
public:
	explicit WelcomeMessages(not_null<Main::Session*> session);
	WelcomeMessages(const WelcomeMessages &other) = delete;
	WelcomeMessages &operator=(const WelcomeMessages &other) = delete;
	~WelcomeMessages();

	[[nodiscard]] int32 lookupId(not_null<const HistoryItem*> item) const;
	[[nodiscard]] HistoryItem *lookupItem(
		not_null<PeerData*> peer,
		int32 ephemeralId) const;
	[[nodiscard]] int count(not_null<History*> history) const;

	void applyNew(const MTPDephemeralMessage &data);
	void applyEdit(const MTPDephemeralMessage &data);
	bool applyDelete(not_null<PeerData*> peer, int32 ephemeralId);

	void send(not_null<History*> history, TextWithEntities text);
	void edit(
		not_null<History*> history,
		int32 ephemeralId,
		TextWithEntities text,
		Fn<void()> done,
		Fn<void(const QString &)> fail);
	void deleteTemplate(not_null<HistoryItem*> item);
	void deleteAll(not_null<History*> history);

	[[nodiscard]] rpl::producer<> updates(not_null<History*> history);
	[[nodiscard]] Data::MessagesSlice list(
		not_null<History*> history) const;

	void clear();

private:
	using OwnedItem = std::unique_ptr<HistoryItem, HistoryItem::Destroyer>;
	struct List {
		std::vector<OwnedItem> items;
		base::flat_map<int32, not_null<HistoryItem*>> itemById;
	};
	struct Request {
		mtpRequestId requestId = 0;
		crl::time lastReceived = 0;
	};

	void request(not_null<History*> history);
	void parse(
		not_null<History*> history,
		const MTPDephemeral_welcomeMessages &data);
	HistoryItem *append(
		not_null<History*> history,
		List &list,
		const MTPDephemeralMessage &data);
	void applyEdition(
		not_null<HistoryItem*> item,
		const MTPDephemeralMessage &data);
	void updated(
		not_null<History*> history,
		const base::flat_set<not_null<HistoryItem*>> &added,
		const base::flat_set<not_null<HistoryItem*>> &clear);
	void sort(List &list);
	void remove(not_null<const HistoryItem*> item);
	void clearOldRequests();

	const not_null<Main::Session*> _session;

	base::Timer _clearTimer;
	base::flat_map<not_null<History*>, List> _data;
	base::flat_map<not_null<History*>, Request> _requests;
	base::flat_map<not_null<History*>, uint64> _hashes;
	rpl::event_stream<not_null<History*>> _updates;

	rpl::lifetime _lifetime;

};

} // namespace Data
