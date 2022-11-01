/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/storage_shared_media.h"

#include <rpl/map.h>

namespace Storage {

auto SharedMedia::enforceLists(Key key)
-> std::map<Key, SharedMedia::Lists>::iterator {
	auto result = _lists.find(key);
	if (result != _lists.end()) {
		return result;
	}
	result = _lists.emplace(key, Lists {}).first;
	for (auto index = 0; index != kSharedMediaTypeCount; ++index) {
		auto &list = result->second[index];
		auto type = static_cast<SharedMediaType>(index);

		list.sliceUpdated(
		) | rpl::map([=](const SparseIdsSliceUpdate &update) {
			return SharedMediaSliceUpdate(
				key.peerId,
				key.topicRootId,
				type,
				update);
		}) | rpl::start_to_stream(_sliceUpdated, _lifetime);
	}
	return result;
}

void SharedMedia::add(SharedMediaAddNew &&query) {
	auto peerIt = enforceLists({ query.peerId, MsgId(0) });
	while (peerIt != end(_lists) && peerIt->first.peerId == query.peerId) {
		for (auto index = 0; index != kSharedMediaTypeCount; ++index) {
			auto type = static_cast<SharedMediaType>(index);
			if (query.types.test(type)) {
				peerIt->second[index].addNew(query.messageId);
			}
		}
		++peerIt;
	}
}

void SharedMedia::add(SharedMediaAddExisting &&query) {
	auto peerIt = enforceLists({ query.peerId, query.topicRootId });
	for (auto index = 0; index != kSharedMediaTypeCount; ++index) {
		auto type = static_cast<SharedMediaType>(index);
		if (query.types.test(type)) {
			peerIt->second[index].addExisting(
				query.messageId,
				query.noSkipRange);
		}
	}
}

void SharedMedia::add(SharedMediaAddSlice &&query) {
	Expects(IsValidSharedMediaType(query.type));

	auto peerIt = enforceLists({ query.peerId, query.topicRootId });
	auto index = static_cast<int>(query.type);
	peerIt->second[index].addSlice(
		std::move(query.messageIds),
		query.noSkipRange,
		query.count);
}

void SharedMedia::remove(SharedMediaRemoveOne &&query) {
	auto peerIt = _lists.lower_bound({ query.peerId, MsgId(0) });
	while (peerIt != end(_lists) && peerIt->first.peerId == query.peerId) {
		for (auto index = 0; index != kSharedMediaTypeCount; ++index) {
			auto type = static_cast<SharedMediaType>(index);
			if (query.types.test(type)) {
				peerIt->second[index].removeOne(query.messageId);
			}
		}
		++peerIt;
	}
	_oneRemoved.fire(std::move(query));
}

void SharedMedia::remove(SharedMediaRemoveAll &&query) {
	auto peerIt = _lists.lower_bound({ query.peerId, query.topicRootId });
	while (peerIt != end(_lists)
		&& peerIt->first.peerId == query.peerId
		&& (!query.topicRootId
			|| peerIt->first.topicRootId == query.topicRootId)) {
		for (auto index = 0; index != kSharedMediaTypeCount; ++index) {
			auto type = static_cast<SharedMediaType>(index);
			if (query.types.test(type)) {
				peerIt->second[index].removeAll();
			}
		}
		++peerIt;
	}
	_allRemoved.fire(std::move(query));
}

void SharedMedia::invalidate(SharedMediaInvalidateBottom &&query) {
	auto peerIt = _lists.lower_bound({ query.peerId, MsgId(0) });
	while (peerIt != end(_lists) && peerIt->first.peerId == query.peerId) {
		for (auto index = 0; index != kSharedMediaTypeCount; ++index) {
			peerIt->second[index].invalidateBottom();
		}
		++peerIt;
	}
	_bottomInvalidated.fire(std::move(query));
}

void SharedMedia::unload(SharedMediaUnloadThread &&query) {
	_lists.erase({ query.peerId, query.topicRootId });
}

rpl::producer<SharedMediaResult> SharedMedia::query(SharedMediaQuery &&query) const {
	Expects(IsValidSharedMediaType(query.key.type));

	auto peerIt = _lists.find({ query.key.peerId, query.key.topicRootId });
	if (peerIt != _lists.end()) {
		auto index = static_cast<int>(query.key.type);
		return peerIt->second[index].query(SparseIdsListQuery(
			query.key.messageId,
			query.limitBefore,
			query.limitAfter));
	}
	return [](auto consumer) {
		consumer.put_done();
		return rpl::lifetime();
	};
}

SharedMediaResult SharedMedia::snapshot(const SharedMediaQuery &query) const {
	Expects(IsValidSharedMediaType(query.key.type));

	auto peerIt = _lists.find({ query.key.peerId, query.key.topicRootId });
	if (peerIt != _lists.end()) {
		auto index = static_cast<int>(query.key.type);
		return peerIt->second[index].snapshot(SparseIdsListQuery(
			query.key.messageId,
			query.limitBefore,
			query.limitAfter));
	}
	return {};
}

bool SharedMedia::empty(const SharedMediaKey &key) const {
	Expects(IsValidSharedMediaType(key.type));

	auto peerIt = _lists.find({ key.peerId, key.topicRootId });
	if (peerIt != _lists.end()) {
		auto index = static_cast<int>(key.type);
		return peerIt->second[index].empty();
	}
	return true;
}

rpl::producer<SharedMediaSliceUpdate> SharedMedia::sliceUpdated() const {
	return _sliceUpdated.events();
}

rpl::producer<SharedMediaRemoveOne> SharedMedia::oneRemoved() const {
	return _oneRemoved.events();
}

rpl::producer<SharedMediaRemoveAll> SharedMedia::allRemoved() const {
	return _allRemoved.events();
}

rpl::producer<SharedMediaInvalidateBottom> SharedMedia::bottomInvalidated() const {
	return _bottomInvalidated.events();
}

} // namespace Storage
