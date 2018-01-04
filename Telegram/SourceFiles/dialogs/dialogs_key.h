/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/value_ordering.h"

namespace Data {
class Feed;
} // namespace Data

namespace Dialogs {

class Key {
public:
	Key() = default;
	Key(History *history) : _value(history) {
	}
	Key(not_null<History*> history) : _value(history) {
	}
	Key(Data::Feed *feed) : _value(feed) {
	}
	Key(not_null<Data::Feed*> feed) : _value(feed) {
	}

	explicit operator bool() const {
		return !!_value;
	}
	History *history() const;
	Data::Feed *feed() const;

	const QString &name() const;
	const PeerData::NameFirstChars &nameFirstChars() const;
	uint64 sortKey() const;
	void cachePinnedIndex(int index) const;

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

	base::optional_variant<
		not_null<History*>,
		not_null<Data::Feed*>> raw() const {
		return _value;
	}

	// Not working :(
	//friend inline auto value_ordering_helper(const Key &key) {
	//	return key.value;
	//}

private:
	base::optional_variant<not_null<History*>, not_null<Data::Feed*>> _value;

};

struct RowDescriptor {
	RowDescriptor() = default;
	RowDescriptor(Key key, MsgId msgId) : key(key), msgId(msgId) {
	}

	Key key;
	MsgId msgId = 0;
};

} // namespace Dialogs
