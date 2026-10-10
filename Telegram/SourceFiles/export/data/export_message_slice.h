/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "export/data/export_data_types.h"
#include "export/export_settings.h"

namespace Export::Data {

[[nodiscard]] inline bool TrimMessagesSliceByDateRange(
		MessagesSlice &slice,
		const Settings &settings) {
	// Topic pagination needs the unfiltered root, cursor and message count.
	if (settings.onlySingleTopic()) {
		return false;
	}
	auto &list = slice.list;

	// Export slices are processed oldest-to-newest, so messages older than
	// the requested range are grouped at the front, newer ones at the back.
	auto from = 0;
	const auto size = int(list.size());
	while (from < size
		&& settings.singlePeerFrom > 0
		&& list[from].date < settings.singlePeerFrom) {
		++from;
	}

	auto till = size;
	while (till > from
		&& settings.singlePeerTill > 0
		&& list[till - 1].date >= settings.singlePeerTill) {
		--till;
	}

	const auto reachedUpperBound = (till < size);
	if (from > 0) {
		list.erase(begin(list), begin(list) + from);
	}
	if (till < size) {
		list.erase(begin(list) + (till - from), end(list));
	}
	return reachedUpperBound;
}

} // namespace Export::Data
