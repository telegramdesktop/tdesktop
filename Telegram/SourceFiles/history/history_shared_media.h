/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "storage/storage_shared_media.h"
#include "mtproto/sender.h"
#include "base/weak_unique_ptr.h"

base::optional<Storage::SharedMediaType> SharedMediaOverviewType(
	Storage::SharedMediaType type);
void SharedMediaShowOverview(
	Storage::SharedMediaType type,
	not_null<History*> history);

class SharedMediaViewer;
class SharedMediaSlice {
public:
	using Key = Storage::SharedMediaKey;

	SharedMediaSlice(
		Key key,
		base::optional<int> fullCount = base::none)
		: _key(key)
		, _fullCount(fullCount) {
	}

	base::optional<int> fullCount() const {
		return _fullCount;
	}
	base::optional<int> skippedBefore() const {
		return _skippedBefore;
	}
	base::optional<int> skippedAfter() const {
		return _skippedAfter;
	}
	base::optional<int> indexOf(MsgId msgId) const {
		auto it = _ids.find(msgId);
		if (it != _ids.end()) {
			return (it - _ids.begin());
		}
		return base::none;
	}
	int size() const {
		return _ids.size();
	}

	using iterator = base::flat_set<MsgId>::const_iterator;

	iterator begin() const {
		return _ids.begin();
	}
	iterator end() const {
		return _ids.end();
	}
	iterator cbegin() const {
		return begin();
	}
	iterator cend() const {
		return end();
	}

	base::optional<int> distance(const Key &a, const Key &b) const {
		if (a.type != _key.type
			|| b.type != _key.type
			|| a.peerId != _key.peerId
			|| b.peerId != _key.peerId) {
			return base::none;
		}
		auto i = _ids.find(a.messageId);
		auto j = _ids.find(b.messageId);
		if (i == _ids.end() || j == _ids.end()) {
			return base::none;
		}
		return j - i;
	}

private:
	Key _key;
	base::flat_set<MsgId> _ids;
	MsgRange _range;
	base::optional<int> _fullCount;
	base::optional<int> _skippedBefore;
	base::optional<int> _skippedAfter;

	friend class SharedMediaViewer;

};

class SharedMediaViewer :
	private base::Subscriber,
	public base::enable_weak_from_this {
public:
	using Type = Storage::SharedMediaType;
	using Key = Storage::SharedMediaKey;

	SharedMediaViewer(
		Key key,
		int limitBefore,
		int limitAfter);

	void start();

	base::Observable<SharedMediaSlice> updated;

private:
	using InitialResult = Storage::SharedMediaResult;
	using SliceUpdate = Storage::SharedMediaSliceUpdate;
	using OneRemoved = Storage::SharedMediaRemoveOne;
	using AllRemoved = Storage::SharedMediaRemoveAll;

	void loadInitial();
	enum class RequestDirection {
		Before,
		After,
	};
	void requestMessages(RequestDirection direction);
	void applyStoredResult(InitialResult &&result);
	void applyUpdate(const SliceUpdate &update);
	void applyUpdate(const OneRemoved &update);
	void applyUpdate(const AllRemoved &update);
	void sliceToLimits();

	void mergeSliceData(
		base::optional<int> count,
		const base::flat_set<MsgId> &messageIds,
		base::optional<int> skippedBefore = base::none,
		base::optional<int> skippedAfter = base::none);


	Key _key;
	int _limitBefore = 0;
	int _limitAfter = 0;
	mtpRequestId _beforeRequestId = 0;
	mtpRequestId _afterRequestId = 0;
	SharedMediaSlice _data;

};
//
//class SharedMediaSliceMerged :
//	private MTP::Sender,
//	private base::Subscriber,
//	public base::enable_weak_from_this {
//public:
//	class Data;
//
//private:
//	friend class Data;
//	using UniversalMsgId = MsgId;
//
//public:
//	using Type = Storage::SharedMediaType;
//
//	SharedMediaSliceMerged(
//		Type type,
//		not_null<History*> history,
//		MsgId aroundId,
//		int limitBefore,
//		int limitAfter);
//
//	bool hasOverview() const;
//	void showOverview() const;
//	bool moveTo(const SharedMediaSliceMerged &other);
//
//	void load();
//
//	class Data {
//	public:
//		base::optional<int> fullCount() const;
//		base::optional<int> skippedBefore() const;
//		base::optional<int> skippedAfter() const;
//		int size() const {
//			return _ids.size();
//		}
//
//		class iterator {
//		public:
//			FullMsgId operator*() const {
//				auto id = _data->_ids[_index];
//				Assert(IsServerMsgId(id)
//					|| (_data->_migrated != nullptr && IsServerMsgId(-id)));
//				return IsServerMsgId(id)
//					? FullMsgId(_data->_history->channelId(), id)
//					: FullMsgId(_data->_migrated->channelId(), -id);
//			}
//			iterator &operator--() {
//				--_index;
//				return *this;
//			}
//			iterator operator--(int) {
//				auto result = *this;
//				--*this;
//				return result;
//			}
//			iterator &operator++() {
//				++_index;
//				return *this;
//			}
//			iterator operator++(int) {
//				auto result = *this;
//				++*this;
//				return result;
//			}
//			iterator &operator+=(int offset) {
//				_index += offset;
//				return *this;
//			}
//			iterator operator+(int offset) const {
//				auto result = *this;
//				return result += offset;
//			}
//			bool operator==(iterator other) const {
//				return (_data == other._data) && (_index == other._index);
//			}
//			bool operator!=(iterator other) const {
//				return !(*this == other);
//			}
//			bool operator<(iterator other) const {
//				return (_data < other._data)
//					|| (_data == other._data && _index < other._index);
//			}
//
//		private:
//			friend class Data;
//
//			iterator(not_null<const Data*> data, int index)
//				: _data(data)
//				, _index(index) {
//			}
//
//			not_null<const Data*> _data;
//			int _index = 0;
//
//		};
//
//		iterator begin() const {
//			return iterator(this, 0);
//		}
//		iterator end() const {
//			iterator(this, _ids.size());
//		}
//		iterator cbegin() const {
//			return begin();
//		}
//		iterator cend() const {
//			return end();
//		}
//
//	private:
//		friend class iterator;
//		friend class SharedMediaSliceMerged;
//
//		Data(
//			not_null<History*> history,
//			History *migrated,
//			base::optional<int> historyCount = base::none,
//			base::optional<int> migratedCount = base::none)
//			: _history(history)
//			, _migrated(migrated)
//			, _historyCount(historyCount)
//			, _migratedCount(migratedCount) {
//			if (!_migrated) {
//				_migratedCount = 0;
//			}
//		}
//
//		not_null<History*> _history;
//		History *_migrated = nullptr;
//		std::vector<UniversalMsgId> _ids;
//		base::optional<int> _historyCount;
//		base::optional<int> _historySkippedBefore;
//		base::optional<int> _historySkippedAfter;
//		base::optional<int> _migratedCount;
//		base::optional<int> _migratedSkippedBefore;
//		base::optional<int> _migratedSkippedAfter;
//
//	};
//	base::Observable<Data> updated;
//
//private:
//	bool amAroundMigrated() const;
//	not_null<History*> aroundHistory() const;
//	MsgId aroundId() const;
//
//	void applyStoredResult(
//		not_null<PeerData*> peer,
//		Storage::SharedMediaResult &&result);
//	bool containsAroundId() const;
//	void clearAfterMove();
//
//	Type _type = Type::kCount;
//	not_null<History*> _history;
//	History *_migrated = nullptr;
//	UniversalMsgId _universalAroundId = 0;
//	int _limitBefore = 0;
//	int _limitAfter = 0;
//	Data _data;
//
//};
