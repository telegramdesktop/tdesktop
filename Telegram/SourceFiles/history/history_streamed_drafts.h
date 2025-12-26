/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"

class History;

class HistoryStreamedDrafts final : public base::has_weak_ptr {
public:
	explicit HistoryStreamedDrafts(not_null<History*> history);
	~HistoryStreamedDrafts();

	[[nodiscard]] rpl::producer<> destroyRequests() const;

	void apply(
		MsgId rootId,
		PeerId fromId,
		TimeId when,
		const MTPDsendMessageTextDraftAction &data);

	void applyItemAdded(not_null<HistoryItem*> item);

private:
	struct Draft {
		not_null<HistoryItem*> message;
		uint64 randomId = 0;
		crl::time updated = 0;
	};

	bool update(MsgId rootId, uint64 randomId, const TextWithEntities &text);
	void clear(MsgId rootId);

	void check();
	void scheduleDestroy();

	const not_null<History*> _history;
	base::flat_map<MsgId, Draft> _drafts;

	base::Timer _checkTimer;

	rpl::event_stream<> _destroyRequests;

};
