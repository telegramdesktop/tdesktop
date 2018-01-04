/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_key.h"

#include "data/data_feed.h"

namespace Dialogs {

const QString &Key::name() const {
	if (const auto h = history()) {
		return h->peer->name;
	}
	// #TODO feeds name
	static const auto empty = QString();
	return empty;
}

const PeerData::NameFirstChars &Key::nameFirstChars() const {
	if (const auto h = history()) {
		return h->peer->nameFirstChars();
	}
	// #TODO feeds name
	static const auto empty = PeerData::NameFirstChars();
	return empty;
}

uint64 Key::sortKey() const {
	if (const auto h = history()) {
		return h->sortKeyInChatList();
	} else if (const auto f = feed()) {
		return f->sortKeyInChatList();
	} else {
		Unexpected("Key value in Key::sortKey");
	}
}

void Key::cachePinnedIndex(int index) const {
	if (const auto h = history()) {
		h->cachePinnedIndex(index);
	} else if (const auto f = feed()) {
		f->cachePinnedIndex(index);
	} else {
		Unexpected("Key value in Key::setPinnedIndex");
	}
}

History *Key::history() const {
	if (const auto p = base::get_if<not_null<History*>>(&_value)) {
		return *p;
	}
	return nullptr;
}

Data::Feed *Key::feed() const {
	if (const auto p = base::get_if<not_null<Data::Feed*>>(&_value)) {
		return *p;
	}
	return nullptr;
}

} // namespace Dialogs
