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

namespace Main {
class Session;
} // namespace Main

namespace Data {

class Session;

struct SponsoredMessage final {
	QByteArray randomId;
	PeerId fromId;
	TextWithEntities textWithEntities;
	History *history = nullptr;
	MsgId msgId;
};

class SponsoredMessages final {
public:
	using RandomId = QByteArray;
	explicit SponsoredMessages(not_null<Session*> owner);
	SponsoredMessages(const SponsoredMessages &other) = delete;
	SponsoredMessages &operator=(const SponsoredMessages &other) = delete;
	~SponsoredMessages();

	void request(not_null<History*> history);
	[[nodiscard]] bool append(not_null<History*> history);
	void clearItems(not_null<History*> history);
	[[nodiscard]] MsgId channelPost(const FullMsgId &fullId) const;

private:
	using OwnedItem = std::unique_ptr<HistoryItem, HistoryItem::Destroyer>;
	struct Entry {
		OwnedItem item;
		SponsoredMessage sponsored;
	};
	struct List {
		std::vector<Entry> entries;
		bool showedAll = false;
	};
	struct Request {
		mtpRequestId requestId = 0;
		crl::time lastReceived = 0;
	};

	void parse(
		not_null<History*> history,
		const MTPmessages_sponsoredMessages &list);
	void append(
		not_null<History*> history,
		List &list,
		const MTPSponsoredMessage &message);
	void clearOldRequests();

	void view(const std::vector<Entry>::iterator entryIt);

	const not_null<Main::Session*> _session;

	base::Timer _clearTimer;
	base::flat_map<not_null<History*>, List> _data;
	base::flat_map<not_null<History*>, Request> _requests;
	base::flat_map<RandomId, Request> _viewRequests;

	rpl::lifetime _lifetime;

};

} // namespace Data
