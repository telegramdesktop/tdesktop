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
	QString chatInviteHash;
};

class SponsoredMessages final {
public:
	struct ChannelPost {
		MsgId msgId;
		std::optional<QString> hash;
	};
	using RandomId = QByteArray;
	explicit SponsoredMessages(not_null<Session*> owner);
	SponsoredMessages(const SponsoredMessages &other) = delete;
	SponsoredMessages &operator=(const SponsoredMessages &other) = delete;
	~SponsoredMessages();

	[[nodiscard]] bool canHaveFor(not_null<History*> history) const;
	void request(not_null<History*> history);
	[[nodiscard]] bool append(not_null<History*> history);
	void clearItems(not_null<History*> history);
	[[nodiscard]] ChannelPost channelPost(const FullMsgId &fullId) const;

	void view(const FullMsgId &fullId);

private:
	using OwnedItem = std::unique_ptr<HistoryItem, HistoryItem::Destroyer>;
	struct Entry {
		OwnedItem item;
		SponsoredMessage sponsored;
	};
	struct List {
		std::vector<Entry> entries;
		bool showedAll = false;
		crl::time received = 0;
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

	const Entry *find(const FullMsgId &fullId) const;

	const not_null<Main::Session*> _session;

	base::Timer _clearTimer;
	base::flat_map<not_null<History*>, List> _data;
	base::flat_map<not_null<History*>, Request> _requests;
	base::flat_map<RandomId, Request> _viewRequests;

	rpl::lifetime _lifetime;

};

} // namespace Data
