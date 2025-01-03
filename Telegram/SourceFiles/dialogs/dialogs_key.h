/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/qt/qt_compare.h"
#include "data/data_message_reaction_id.h"

class History;
class PeerData;

namespace Data {
class Thread;
class Folder;
class ForumTopic;
class SavedSublist;
struct ReactionId;
} // namespace Data

namespace Dialogs {

class Entry;
enum class ChatSearchTab : uchar;

class Key {
public:
	Key() = default;
	Key(Entry *entry) : _value(entry) {
	}
	Key(History *history);
	Key(Data::Folder *folder);
	Key(Data::Thread *thread);
	Key(Data::ForumTopic *topic);
	Key(Data::SavedSublist *sublist);
	Key(not_null<Entry*> entry) : _value(entry) {
	}
	Key(not_null<History*> history);
	Key(not_null<Data::Thread*> thread);
	Key(not_null<Data::Folder*> folder);
	Key(not_null<Data::ForumTopic*> topic);
	Key(not_null<Data::SavedSublist*> sublist);

	explicit operator bool() const {
		return (_value != nullptr);
	}
	[[nodiscard]] not_null<Entry*> entry() const;
	[[nodiscard]] History *history() const;
	[[nodiscard]] Data::Folder *folder() const;
	[[nodiscard]] Data::ForumTopic *topic() const;
	[[nodiscard]] Data::Thread *thread() const;
	[[nodiscard]] History *owningHistory() const;
	[[nodiscard]] PeerData *peer() const;
	[[nodiscard]] Data::SavedSublist *sublist() const;

	friend inline constexpr auto operator<=>(Key, Key) noexcept = default;

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
		SavedSublist,
		ContextMenu,
		ShortcutMessages,
	};

	Key key;
	Section section = Section::History;
	FilterId filterId = 0;
	FullReplyTo currentReplyTo;

	friend inline auto operator<=>(
		const EntryState&,
		const EntryState&) = default;
	friend inline bool operator==(
		const EntryState&,
		const EntryState&) = default;
};

enum class ChatTypeFilter : uchar {
	All,
	Private,
	Groups,
	Channels,
};

struct SearchState {
	Key inChat;
	PeerData *fromPeer = nullptr;
	std::vector<Data::ReactionId> tags;
	ChatSearchTab tab = {};
	ChatTypeFilter filter = ChatTypeFilter::All;
	QString query;

	[[nodiscard]] bool empty() const;
	[[nodiscard]] ChatSearchTab defaultTabForMe() const;
	[[nodiscard]] bool filterChatsList() const;

	explicit operator bool() const {
		return !empty();
	}

	friend inline auto operator<=>(
		const SearchState&,
		const SearchState&) noexcept = default;
	friend inline bool operator==(
		const SearchState&,
		const SearchState&) = default;
};

} // namespace Dialogs
