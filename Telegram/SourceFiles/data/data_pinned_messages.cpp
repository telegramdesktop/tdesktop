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

rpl::producer<PinnedAroundId> PinnedMessages::viewer(
		MsgId aroundId,
		int limit) const {
	return _list.viewer(MessagesQuery{
		.aroundId = position(aroundId),
		.limitBefore = limit,
		.limitAfter = limit
	}) | rpl::map([](const MessagesResult &result) {
		auto data = PinnedAroundId();
		data.fullCount = result.count;
		data.skippedBefore = result.skippedBefore;
		data.skippedAfter = result.skippedAfter;
		data.ids = result.messageIds | ranges::view::transform(
			[](MessagePosition position) { return position.fullId.msg; }
		) | ranges::to_vector;
		return data;
	});
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
		MsgRange range,
		std::optional<int> count) {
	auto positions = ids | ranges::view::transform([&](MsgId id) {
		return position(id);
	}) | ranges::to_vector;

	_list.addSlice(
		std::move(positions),
		MessagesRange{
			.from = range.from ? position(range.from) : MinMessagePosition,
			.till = position(range.till)
		},
		count);
}

void PinnedMessages::remove(MsgId messageId) {
	_list.removeOne(position(messageId));
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
	const auto wrapped = position(messageId);
	_list.addSlice(
		{ wrapped },
		{ .from = wrapped, .till = MaxMessagePosition },
		std::nullopt);
}

void PinnedMessages::clearLessThanId(MsgId messageId) {
	_list.removeLessThan(position(messageId));
}

} // namespace Data
