/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_messages.h"
#include "base/weak_ptr.h"

namespace Storage {
class Facade;
} // namespace Storage

namespace Data {

struct PinnedAroundId {
	std::vector<MsgId> ids;
	std::optional<int> skippedBefore;
	std::optional<int> skippedAfter;
	std::optional<int> fullCount;
};

class PinnedMessages final : public base::has_weak_ptr {
public:
	explicit PinnedMessages(not_null<PeerData*> peer);

	[[nodiscard]] bool empty() const;
	[[nodiscard]] MsgId topId() const;
	[[nodiscard]] rpl::producer<PinnedAroundId> viewer(
		MsgId aroundId,
		int limit) const;
	void setTopId(MsgId messageId);

private:
	const not_null<PeerData*> _peer;
	Storage::Facade &_storage;

};

} // namespace Data
