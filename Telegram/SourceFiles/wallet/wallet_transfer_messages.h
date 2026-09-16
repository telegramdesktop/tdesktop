/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

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
	HistoryItem *adopt(
		not_null<History*> history,
		MsgId id,
		const MTPDmessageService &data);

private:
	struct Entry {
		FullMsgId id;
		uint64 randomId = 0;
	};

	const not_null<Main::Session*> _session;
	std::vector<Entry> _entries;
	rpl::lifetime _lifetime;

};

} // namespace Wallet
