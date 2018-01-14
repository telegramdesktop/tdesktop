/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_types.h"

namespace Data {

struct Group {
	HistoryItemsList items;

};

class Groups {
public:
	void registerMessage(not_null<HistoryItem*> item);
	void unregisterMessage(not_null<HistoryItem*> item);

	const Group *find(not_null<HistoryItem*> item) const;

private:
	std::map<MessageGroupId, Group> _data;
	std::map<MessageGroupId, MessageGroupId> _alias;

};

} // namespace Data
