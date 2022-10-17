/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_key.h"

#include "data/data_folder.h"
#include "data/data_forum_topic.h"
#include "history/history.h"

namespace Dialogs {

Key::Key(History *history) : _value(history) {
}

Key::Key(Data::Folder *folder) : _value(folder) {
}

Key::Key(Data::Thread *thread) : _value(thread) {
}

Key::Key(Data::ForumTopic *topic) : _value(topic) {
}

Key::Key(not_null<History*> history) : _value(history) {
}

Key::Key(not_null<Data::Thread*> thread) : _value(thread) {
}

Key::Key(not_null<Data::Folder*> folder) : _value(folder) {
}

Key::Key(not_null<Data::ForumTopic*> topic) : _value(topic) {
}

not_null<Entry*> Key::entry() const {
	Expects(_value != nullptr);

	return _value;
}

History *Key::history() const {
	return _value ? _value->asHistory() : nullptr;
}

Data::Folder *Key::folder() const {
	return _value ? _value->asFolder() : nullptr;
}

Data::ForumTopic *Key::topic() const {
	return _value ? _value->asTopic() : nullptr;
}

Data::Thread *Key::thread() const {
	return _value ? _value->asThread() : nullptr;
}

History *Key::owningHistory() const {
	if (const auto thread = this->thread()) {
		return thread->owningHistory();
	}
	return nullptr;
}

PeerData *Key::peer() const {
	if (const auto history = owningHistory()) {
		return history->peer;
	}
	return nullptr;
}

} // namespace Dialogs
