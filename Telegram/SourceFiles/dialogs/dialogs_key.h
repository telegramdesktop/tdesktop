/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/value_ordering.h"

class History;
class PeerData;

namespace Data {
class Folder;
} // namespace Data

namespace Dialogs {

class Entry;

class Key {
public:
	Key() = default;
	Key(Entry *entry) : _value(entry) {
	}
	Key(History *history);
	Key(Data::Folder *folder);
	Key(not_null<Entry*> entry) : _value(entry) {
	}
	Key(not_null<History*> history);
	Key(not_null<Data::Folder*> folder);

	explicit operator bool() const {
		return (_value != nullptr);
	}
	not_null<Entry*> entry() const;
	History *history() const;
	Data::Folder *folder() const;
	PeerData *peer() const;

	constexpr auto operator<=>(const Key &other) const = default;
	constexpr bool operator==(const Key &other) const = default;

private:
	Entry *_value = nullptr;

};

struct RowDescriptor {
	RowDescriptor() = default;
	RowDescriptor(Key key, FullMsgId fullId) : key(key), fullId(fullId) {
	}

	constexpr auto operator<=>(const RowDescriptor &other) const noexcept {
		if (const auto result = (key <=> other.key); result != 0) {
			return result;
		} else if (!fullId.msg && !other.fullId.msg) {
			return (fullId.msg <=> other.fullId.msg);
		} else {
			return (fullId <=> other.fullId);
		}
	}
	constexpr bool operator==(const RowDescriptor &other) const noexcept {
		return ((*this) <=> other) == 0;
	}

	Key key;
	FullMsgId fullId;

};

struct EntryState {
	enum class Section {
		History,
		Profile,
		ChatsList,
		Scheduled,
		Pinned,
		Replies,
	};

	Key key;
	Section section = Section::History;
	FilterId filterId = 0;
	MsgId rootId = 0;
	MsgId currentReplyToId = 0;
};

} // namespace Dialogs
