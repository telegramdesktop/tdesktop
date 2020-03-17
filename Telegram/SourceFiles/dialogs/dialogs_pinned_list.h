/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class History;

namespace Data {
class Session;
} // namespace Data

namespace Dialogs {

class Key;

class PinnedList final {
public:
	PinnedList(FilterId filterId, int limit);

	void setLimit(int limit);

	// Places on the last place in the list otherwise.
	// Does nothing if already pinned.
	void addPinned(const Key &key);

	// if (pinned) places on the first place in the list.
	void setPinned(const Key &key, bool pinned);

	void clear();

	void applyList(
		not_null<Data::Session*> owner,
		const QVector<MTPDialogPeer> &list);
	void applyList(const std::vector<not_null<History*>> &list);
	void reorder(const Key &key1, const Key &key2);

	const std::vector<Key> &order() const {
		return _data;
	}

private:
	int addPinnedGetPosition(const Key &key);
	void applyLimit(int limit);

	FilterId _filterId = 0;
	int _limit = 0;
	std::vector<Key> _data;

};

} // namespace Dialogs
