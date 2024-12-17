/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/media/info_media_common.h"

#include "history/history_item.h"
#include "storage/storage_shared_media.h"
#include "styles/style_info.h"
#include "styles/style_overview.h"

namespace Info::Media {

UniversalMsgId GetUniversalId(FullMsgId itemId) {
	return peerIsChannel(itemId.peer)
		? UniversalMsgId(itemId.msg)
		: UniversalMsgId(itemId.msg - ServerMaxMsgId);
}

UniversalMsgId GetUniversalId(not_null<const HistoryItem*> item) {
	return GetUniversalId(item->fullId());
}

UniversalMsgId GetUniversalId(not_null<const BaseLayout*> layout) {
	return GetUniversalId(layout->getItem()->fullId());
}

bool ChangeItemSelection(
		ListSelectedMap &selected,
		not_null<const HistoryItem*> item,
		ListItemSelectionData selectionData) {
	const auto changeExisting = [&](auto it) {
		if (it == selected.cend()) {
			return false;
		} else if (it->second != selectionData) {
			it->second = selectionData;
			return true;
		}
		return false;
	};
	if (selected.size() < MaxSelectedItems) {
		const auto &[i, ok] = selected.try_emplace(item, selectionData);
		if (ok) {
			return true;
		}
		return changeExisting(i);
	}
	return changeExisting(selected.find(item));
}

int MinItemHeight(Type type, int width) {
	auto &songSt = st::overviewFileLayout;

	switch (type) {
	case Type::Photo:
	case Type::GIF:
	case Type::Video:
	case Type::RoundFile: {
		auto itemsLeft = st::infoMediaSkip;
		auto itemsInRow = (width - itemsLeft)
			/ (st::infoMediaMinGridSize + st::infoMediaSkip);
		return (st::infoMediaMinGridSize + st::infoMediaSkip) / itemsInRow;
	} break;

	case Type::RoundVoiceFile:
		return songSt.songPadding.top()
			+ songSt.songThumbSize
			+ songSt.songPadding.bottom()
			+ st::lineWidth;
	case Type::File:
		return songSt.filePadding.top()
			+ songSt.fileThumbSize
			+ songSt.filePadding.bottom()
			+ st::lineWidth;
	case Type::MusicFile:
		return songSt.songPadding.top()
			+ songSt.songThumbSize
			+ songSt.songPadding.bottom();
	case Type::Link:
		return st::linksPhotoSize
			+ st::linksMargin.top()
			+ st::linksMargin.bottom()
			+ st::linksBorder;
	}
	Unexpected("Type in MinItemHeight()");
}
} // namespace Info::Media
