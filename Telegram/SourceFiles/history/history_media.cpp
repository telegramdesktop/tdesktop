/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_media.h"

#include "storage/storage_shared_media.h"

Storage::SharedMediaTypesMask HistoryMedia::sharedMediaTypes() const {
	return {};
}
