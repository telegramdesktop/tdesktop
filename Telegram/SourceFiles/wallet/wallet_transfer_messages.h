/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_set.h"
#include "data/data_msg_id.h"

class History;
class HistoryItem;
class MTPDmessageService;

namespace Main {
class Session;
} // namespace Main

namespace Wallet {

struct SendArgs;

class TransferMessages final {
public:
	explicit TransferMessages(not_null<Main::Session*> session);

	[[nodiscard]] FullMsgId create(const SendArgs &args, uint64 randomId);
	void dropSending(FullMsgId id);
	void failSending(FullMsgId id, const QString &reason = QString());
	[[nodiscard]] bool refusePairing(
		not_null<HistoryItem*> item,
		MsgId newId);
	HistoryItem *adopt(
		not_null<History*> history,
		MsgId id,
		const MTPDmessageService &data);

private:
	struct Entry {
		FullMsgId id;
		uint64 randomId = 0;
		MsgId floor;
	};

	[[nodiscard]] HistoryItem *forgetSending(FullMsgId id);
	std::vector<Entry>::iterator forget(std::vector<Entry>::iterator i);
	void settle(
		not_null<HistoryItem*> item,
		MsgId id,
		const MTPDmessageService &data);

	const not_null<Main::Session*> _session;
	std::vector<Entry> _entries;
	base::flat_set<FullMsgId> _settled;
	rpl::lifetime _lifetime;

};

} // namespace Wallet
