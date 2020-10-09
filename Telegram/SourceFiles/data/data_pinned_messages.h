/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_messages.h"
#include "base/weak_ptr.h"

namespace Data {

struct PinnedAroundId {
	std::vector<MsgId> ids;
	std::optional<int> skippedBefore;
	std::optional<int> skippedAfter;
	std::optional<int> fullCount;
};

class PinnedMessages final : public base::has_weak_ptr {
public:
	explicit PinnedMessages(ChannelId channelId);

	[[nodiscard]] bool empty() const;
	[[nodiscard]] MsgId topId() const;
	[[nodiscard]] rpl::producer<PinnedAroundId> viewer(
		MsgId aroundId,
		int limit) const;

	void add(MsgId messageId);
	void add(
		std::vector<MsgId> &&ids,
		MsgRange range,
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
