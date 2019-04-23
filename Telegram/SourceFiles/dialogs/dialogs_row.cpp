/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_row.h"

#include "ui/effects/ripple_animation.h"
#include "ui/text_options.h"
#include "dialogs/dialogs_entry.h"
#include "data/data_folder.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "styles/style_dialogs.h"

namespace Dialogs {
namespace {

QString ComposeFolderListEntryText(not_null<Data::Folder*> folder) {
	const auto &list = folder->lastUnreadHistories();
	if (list.empty()) {
		const auto count = folder->chatsListSize();
		if (!count) {
			return QString();
		}
		return lng_archived_chats(lt_count, count);
	}
	const auto count = std::max(
		int(list.size()),
		folder->unreadHistoriesCount());
	if (list.size() == 1) {
		return App::peerName(list[0]->peer);
	} else if (count == 2) {
		return lng_archived_unread_two(
			lt_chat,
			App::peerName(list[0]->peer),
			lt_second_chat,
			App::peerName(list[1]->peer));
	}
	return lng_archived_unread(
		lt_count,
		count - 2,
		lt_chat,
		App::peerName(list[0]->peer),
		lt_second_chat,
		App::peerName(list[1]->peer));
}

} // namespace

RippleRow::RippleRow() = default;
RippleRow::~RippleRow() = default;

void RippleRow::addRipple(QPoint origin, QSize size, Fn<void()> updateCallback) {
	if (!_ripple) {
		auto mask = Ui::RippleAnimation::rectMask(size);
		_ripple = std::make_unique<Ui::RippleAnimation>(st::dialogsRipple, std::move(mask), std::move(updateCallback));
	}
	_ripple->add(origin);
}

void RippleRow::stopLastRipple() {
	if (_ripple) {
		_ripple->lastStop();
	}
}

void RippleRow::paintRipple(Painter &p, int x, int y, int outerWidth, const QColor *colorOverride) const {
	if (_ripple) {
		_ripple->paint(p, x, y, outerWidth, colorOverride);
		if (_ripple->empty()) {
			_ripple.reset();
		}
	}
}

uint64 Row::sortKey() const {
	return _id.entry()->sortKeyInChatList();
}

void Row::validateListEntryCache() const {
	const auto folder = _id.folder();
	if (!folder) {
		return;
	}
	const auto version = folder->chatListViewVersion();
	if (_listEntryCacheVersion == version) {
		return;
	}
	_listEntryCacheVersion = version;
	_listEntryCache.setText(
		st::dialogsTextStyle,
		ComposeFolderListEntryText(folder),
		Ui::DialogTextOptions());
}

FakeRow::FakeRow(Key searchInChat, not_null<HistoryItem*> item)
: _searchInChat(searchInChat)
, _item(item)
, _cache(st::dialogsTextWidthMin) {
}

} // namespace Dialogs