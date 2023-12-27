/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_saved_sublist.h"

#include "data/data_histories.h"
#include "data/data_peer.h"
#include "data/data_saved_messages.h"
#include "data/data_session.h"
#include "history/view/history_view_item_preview.h"
#include "history/history.h"
#include "history/history_item.h"

namespace Data {

SavedSublist::SavedSublist(not_null<PeerData*> peer)
: Entry(&peer->owner(), Dialogs::Entry::Type::SavedSublist)
, _history(peer->owner().history(peer)) {
}

SavedSublist::~SavedSublist() = default;

not_null<History*> SavedSublist::history() const {
	return _history;
}

not_null<PeerData*> SavedSublist::peer() const {
	return _history->peer;
}

bool SavedSublist::isHiddenAuthor() const {
	return peer()->isSavedHiddenAuthor();
}

bool SavedSublist::isFullLoaded() const {
	return (_flags & Flag::FullLoaded) != 0;
}

auto SavedSublist::messages() const
-> const std::vector<not_null<HistoryItem*>> & {
	return _items;
}

void SavedSublist::applyMaybeLast(not_null<HistoryItem*> item) {
	const auto before = [](
			not_null<HistoryItem*> a,
			not_null<HistoryItem*> b) {
		return IsServerMsgId(a->id)
			? (IsServerMsgId(b->id) ? (a->id < b->id) : true)
			: (IsServerMsgId(b->id) ? false : (a->id < b->id));
	};

	if (_items.empty()) {
		_items.push_back(item);
	} else if (_items.front() == item) {
		return;
	} else if (_items.size() == 1 && before(_items.front(), item)) {
		_items[0] = item;
	} else if (before(_items.back(), item)) {
		for (auto i = begin(_items); i != end(_items); ++i) {
			if (item == *i) {
				break;
			} else if (before(*i, item)) {
				_items.insert(i, item);
				break;
			}
		}
	}
	if (_items.front() == item) {
		setChatListTimeId(item->date());
		resolveChatListMessageGroup();
	}
}

void SavedSublist::removeOne(not_null<HistoryItem*> item) {
	if (_items.empty()) {
		return;
	}
	const auto last = (_items.front() == item);
	_items.erase(ranges::remove(_items, item), end(_items));
	if (last) {
		if (_items.empty()) {
			if (isFullLoaded()) {
				updateChatListExistence();
			} else {
				updateChatListEntry();
				crl::on_main(this, [=] {
					owner().savedMessages().loadMore(this);
				});
			}
		} else {
			setChatListTimeId(_items.front()->date());
		}
	}
}

void SavedSublist::append(std::vector<not_null<HistoryItem*>> &&items) {
	if (items.empty()) {
		setFullLoaded();
	} else if (!_items.empty()) {
		_items.insert(end(_items), begin(items), end(items));
	} else {
		_items = std::move(items);
		setChatListTimeId(_items.front()->date());
	}
}

void SavedSublist::setFullLoaded(bool loaded) {
	if (loaded != isFullLoaded()) {
		if (loaded) {
			_flags |= Flag::FullLoaded;
			if (_items.empty()) {
				updateChatListExistence();
			}
		} else {
			_flags &= ~Flag::FullLoaded;
		}
	}
}

int SavedSublist::fixedOnTopIndex() const {
	return 0;
}

bool SavedSublist::shouldBeInChatList() const {
	return isPinnedDialog(FilterId()) || !_items.empty();
}

Dialogs::UnreadState SavedSublist::chatListUnreadState() const {
	return {};
}

Dialogs::BadgesState SavedSublist::chatListBadgesState() const {
	return {};
}

HistoryItem *SavedSublist::chatListMessage() const {
	return _items.empty() ? nullptr : _items.front().get();
}

bool SavedSublist::chatListMessageKnown() const {
	return true;
}

const QString &SavedSublist::chatListName() const {
	return _history->chatListName();
}

const base::flat_set<QString> &SavedSublist::chatListNameWords() const {
	return _history->chatListNameWords();
}

const base::flat_set<QChar> &SavedSublist::chatListFirstLetters() const {
	return _history->chatListFirstLetters();
}

const QString &SavedSublist::chatListNameSortKey() const {
	return _history->chatListNameSortKey();
}

int SavedSublist::chatListNameVersion() const {
	return _history->chatListNameVersion();
}

void SavedSublist::paintUserpic(
		Painter &p,
		Ui::PeerUserpicView &view,
		const Dialogs::Ui::PaintContext &context) const {
	_history->paintUserpic(p, view, context);
}

void SavedSublist::chatListPreloadData() {
	peer()->loadUserpic();
	allowChatListMessageResolve();
}

void SavedSublist::allowChatListMessageResolve() {
	if (_flags & Flag::ResolveChatListMessage) {
		return;
	}
	_flags |= Flag::ResolveChatListMessage;
	resolveChatListMessageGroup();
}

bool SavedSublist::hasOrphanMediaGroupPart() const {
	if (isFullLoaded() || _items.size() != 1) {
		return false;
	}
	return (_items.front()->groupId() != MessageGroupId());
}

void SavedSublist::resolveChatListMessageGroup() {
	const auto item = chatListMessage();
	if (!(_flags & Flag::ResolveChatListMessage)
		|| !item
		|| !hasOrphanMediaGroupPart()) {
		return;
	}
	// If we set a single album part, request the full album.
	const auto withImages = !item->toPreview({
		.hideSender = true,
		.hideCaption = true }).images.empty();
	if (withImages) {
		owner().histories().requestGroupAround(item);
	}
}

} // namespace Data
