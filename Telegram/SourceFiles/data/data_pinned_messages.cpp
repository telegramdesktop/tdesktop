/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_pinned_messages.h"

namespace Data {

PinnedMessages::PinnedMessages(ChannelId channelId) : _channelId(channelId) {
}

bool PinnedMessages::empty() const {
	return _list.empty();
}

MsgId PinnedMessages::topId() const {
	const auto slice = _list.snapshot(MessagesQuery{
		.aroundId = MaxMessagePosition,
		.limitBefore = 1,
		.limitAfter = 1
	});
	return slice.messageIds.empty() ? 0 : slice.messageIds.back().fullId.msg;
}

MessagePosition PinnedMessages::position(MsgId id) const {
	return MessagePosition{
	   .fullId = FullMsgId(_channelId, id),
	};
}

void PinnedMessages::add(MsgId messageId) {
	_list.addOne(position(messageId));
}

void PinnedMessages::add(
		std::vector<MsgId> &&ids,
		MsgId from,
		MsgId till,
		std::optional<int> count) {
	auto positions = ids | ranges::view::transform([&](MsgId id) {
		return position(id);
	}) | ranges::to_vector;

	_list.addSlice(
		std::move(positions),
		MessagesRange{
			.from = from ? position(from) : MinMessagePosition,
			.till = position(till)
		},
		count);
}

void PinnedMessages::remove(MsgId messageId) {
	_list.removeOne(MessagePosition{
		.fullId = FullMsgId(0, messageId),
	});
}

void PinnedMessages::setTopId(MsgId messageId) {
	while (true) {
		auto top = topId();
		if (top > messageId) {
			remove(top);
		} else if (top == messageId) {
			return;
		} else {
			break;
		}
	}
	const auto position = MessagePosition{
		.fullId = FullMsgId(0, messageId),
	};
	_list.addSlice(
		{ position },
		{ .from = position, .till = MaxMessagePosition },
		std::nullopt);
}

void PinnedMessages::clearLessThanId(MsgId messageId) {
	_list.removeLessThan(MessagePosition{
		.fullId = FullMsgId(0, messageId),
	});
}

} // namespace Data
