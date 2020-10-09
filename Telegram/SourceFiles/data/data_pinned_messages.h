/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_messages.h"

namespace Data {

class PinnedMessages final {
public:
	explicit PinnedMessages(ChannelId channelId);

	[[nodiscard]] bool empty() const;
	[[nodiscard]] MsgId topId() const;

	void add(MsgId messageId);
	void add(
		std::vector<MsgId> &&ids,
		MsgId from,
		MsgId till,
		std::optional<int> count);
	void remove(MsgId messageId);

	void setTopId(MsgId messageId);

	void clearLessThanId(MsgId messageId);

private:
	[[nodiscard]] MessagePosition position(MsgId id) const;

	MessagesList _list;
	ChannelId _channelId = 0;

};

} // namespace Data
