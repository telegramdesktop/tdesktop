/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_map.h"

namespace MTP::details {

// Received msgIds and wereAcked msgIds count stored.
inline constexpr auto kIdsBufferSize = 400;

class ReceivedIdsManager final {
public:
	enum class State {
		NotFound,
		NeedsAck,
		NoAckNeeded,
	};
	enum class Result {
		Success,
		Duplicate,
		TooOld,
	};

	[[nodiscard]] Result registerMsgId(mtpMsgId msgId, bool needAck);
	[[nodiscard]] mtpMsgId min() const;
	[[nodiscard]] mtpMsgId max() const;
	[[nodiscard]] State lookup(mtpMsgId msgId) const;

	void shrink();
	void clear();

private:
	base::flat_map<mtpMsgId, bool> _idsNeedAck;

};

} // namespace MTP::details
