/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_key.h"

#include "data/data_folder.h"
#include "history/history.h"

namespace Dialogs {
namespace {

using Folder = Data::Folder;

} // namespace

Key::Key(History *history) : _value(history) {
}

Key::Key(Data::Folder *folder) : _value(folder) {
}

Key::Key(not_null<History*> history) : _value(history) {
}

Key::Key(not_null<Data::Folder*> folder) : _value(folder) {
}

not_null<Entry*> Key::entry() const {
	Expects(_value != nullptr);

	return _value;
}

History *Key::history() const {
	return _value ? _value->asHistory() : nullptr;
}

Folder *Key::folder() const {
	return _value ? _value->asFolder() : nullptr;
}

PeerData *Key::peer() const {
	if (const auto history = this->history()) {
		return history->peer;
	}
	return nullptr;
}

} // namespace Dialogs
