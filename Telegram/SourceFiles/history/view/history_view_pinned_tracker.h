/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/history_view_pinned_bar.h"

class History;

namespace Data {
enum class LoadDirection : char;
class Thread;
} // namespace Data

namespace HistoryView {

class PinnedTracker final {
public:
	using UniversalMsgId = MsgId;

	explicit PinnedTracker(not_null<Data::Thread*> thread);
	~PinnedTracker();

	[[nodiscard]] rpl::producer<PinnedId> shownMessageId() const;
	[[nodiscard]] PinnedId currentMessageId() const;
	void trackAround(UniversalMsgId messageId);
	void reset();

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	struct Slice {
		std::vector<FullMsgId> ids;
		std::optional<int> fullCount;
		std::optional<int> skippedBefore;
		std::optional<int> skippedAfter;
	};
	void clear();
	void refreshViewer();
	void refreshCurrentFromSlice();

	const not_null<Data::Thread*> _thread;
	PeerData *_migratedPeer = nullptr;

	rpl::variable<PinnedId> _current;
	rpl::lifetime _dataLifetime;

	UniversalMsgId _aroundId = 0;
	UniversalMsgId _viewerAroundId = 0;
	Slice _slice;

	rpl::lifetime _lifetime;

};

} // namespace HistoryView
