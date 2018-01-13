/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_key.h"

#include "data/data_feed.h"
#include "history/history.h"

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

not_null<Entry*> Key::entry() const {
	if (const auto p = base::get_if<not_null<History*>>(&_value)) {
		return *p;
	} else if (const auto p = base::get_if<not_null<Data::Feed*>>(&_value)) {
		return *p;
	}
	Unexpected("Dialogs entry() call on empty Key.");
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
