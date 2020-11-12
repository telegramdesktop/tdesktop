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

	inline bool operator<(const Key &other) const {
		return _value < other._value;
	}
	inline bool operator>(const Key &other) const {
		return (other < *this);
	}
	inline bool operator<=(const Key &other) const {
		return !(other < *this);
	}
	inline bool operator>=(const Key &other) const {
		return !(*this < other);
	}
	inline bool operator==(const Key &other) const {
		return _value == other._value;
	}
	inline bool operator!=(const Key &other) const {
		return !(*this == other);
	}

	// Not working :(
	//friend inline auto value_ordering_helper(const Key &key) {
	//	return key.value;
	//}

private:
	Entry *_value = nullptr;

};

struct RowDescriptor {
	RowDescriptor() = default;
	RowDescriptor(Key key, FullMsgId fullId) : key(key), fullId(fullId) {
	}

	Key key;
	FullMsgId fullId;

};

inline bool operator==(const RowDescriptor &a, const RowDescriptor &b) {
	return (a.key == b.key)
		&& ((a.fullId == b.fullId) || (!a.fullId.msg && !b.fullId.msg));
}

inline bool operator!=(const RowDescriptor &a, const RowDescriptor &b) {
	return !(a == b);
}

inline bool operator<(const RowDescriptor &a, const RowDescriptor &b) {
	if (a.key < b.key) {
		return true;
	} else if (a.key > b.key) {
		return false;
	}
	return a.fullId < b.fullId;
}

inline bool operator>(const RowDescriptor &a, const RowDescriptor &b) {
	return (b < a);
}

inline bool operator<=(const RowDescriptor &a, const RowDescriptor &b) {
	return !(b < a);
}

inline bool operator>=(const RowDescriptor &a, const RowDescriptor &b) {
	return !(a < b);
}

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
