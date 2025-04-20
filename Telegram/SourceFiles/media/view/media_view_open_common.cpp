/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/view/media_view_open_common.h"

#include "history/history_item.h"
#include "data/data_media_types.h"
#include "data/data_web_page.h"

namespace Media::View {

TimeId ExtractVideoTimestamp(not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media) {
		return 0;
	} else if (const auto timestamp = media->videoTimestamp()) {
		return timestamp;
	} else if (const auto webpage = media->webpage()) {
		return webpage->extractVideoTimestamp();
	}
	return 0;
}

} // namespace Media::View
