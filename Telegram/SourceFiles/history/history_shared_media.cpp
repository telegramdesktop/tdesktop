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
#include "history/history_shared_media.h"

#include <rpl/combine.h>
#include "auth_session.h"
#include "apiwrap.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"
#include "history/history_media_types.h"
#include "history/history_sparse_ids.h"

namespace {

using Type = Storage::SharedMediaType;

inline MediaOverviewType SharedMediaTypeToOverview(Type type) {
	switch (type) {
	case Type::Photo: return OverviewPhotos;
	case Type::Video: return OverviewVideos;
	case Type::MusicFile: return OverviewMusicFiles;
	case Type::File: return OverviewFiles;
	case Type::VoiceFile: return OverviewVoiceFiles;
	case Type::Link: return OverviewLinks;
	default: break;
	}
	return OverviewCount;
}

} // namespace

base::optional<Storage::SharedMediaType> SharedMediaOverviewType(
	Storage::SharedMediaType type) {
	if (SharedMediaTypeToOverview(type) != OverviewCount) {
		return type;
	}
	return base::none;
}

void SharedMediaShowOverview(
	Storage::SharedMediaType type,
	not_null<History*> history) {
	if (SharedMediaOverviewType(type)) {
		Ui::showPeerOverview(history, SharedMediaTypeToOverview(type));
	}
}

rpl::producer<SparseIdsSlice> SharedMediaViewer(
		Storage::SharedMediaKey key,
		int limitBefore,
		int limitAfter) {
	Expects(IsServerMsgId(key.messageId) || (key.messageId == 0));
	Expects((key.messageId != 0) || (limitBefore == 0 && limitAfter == 0));

	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();
		auto builder = lifetime.make_state<SparseIdsSliceBuilder>(
			key.messageId,
			limitBefore,
			limitAfter);
		auto requestMediaAround = [
			peer = App::peer(key.peerId),
			type = key.type
		](const SparseIdsSliceBuilder::AroundData &data) {
			Auth().api().requestSharedMedia(
				peer,
				type,
				data.first,
				data.second);
		};
		builder->insufficientAround()
			| rpl::start_with_next(requestMediaAround, lifetime);

		auto pushNextSnapshot = [=] {
			consumer.put_next(builder->snapshot());
		};

		using SliceUpdate = Storage::SharedMediaSliceUpdate;
		Auth().storage().sharedMediaSliceUpdated()
			| rpl::filter([=](const SliceUpdate &update) {
				return (update.peerId == key.peerId)
					&& (update.type == key.type);
			})
			| rpl::filter([=](const SliceUpdate &update) {
				return builder->applyUpdate(update.data);
			})
			| rpl::start_with_next(pushNextSnapshot, lifetime);

		using OneRemoved = Storage::SharedMediaRemoveOne;
		Auth().storage().sharedMediaOneRemoved()
			| rpl::filter([=](const OneRemoved &update) {
				return (update.peerId == key.peerId)
					&& update.types.test(key.type);
			})
			| rpl::filter([=](const OneRemoved &update) {
				return builder->removeOne(update.messageId);
			})
			| rpl::start_with_next(pushNextSnapshot, lifetime);

		using AllRemoved = Storage::SharedMediaRemoveAll;
		Auth().storage().sharedMediaAllRemoved()
			| rpl::filter([=](const AllRemoved &update) {
				return (update.peerId == key.peerId);
			})
			| rpl::filter([=](const AllRemoved &update) {
				return builder->removeAll();
			})
			| rpl::start_with_next(pushNextSnapshot, lifetime);

		using Result = Storage::SharedMediaResult;
		Auth().storage().query(
			Storage::SharedMediaQuery(
				key,
				limitBefore,
				limitAfter))
			| rpl::filter([=](const Result &result) {
				return builder->applyInitial(result);
			})
			| rpl::start_with_next_done(
				pushNextSnapshot,
				[=] { builder->checkInsufficient(); },
				lifetime);

		return lifetime;
	};
}

rpl::producer<SparseIdsMergedSlice> SharedMediaMergedViewer(
		SharedMediaMergedKey key,
		int limitBefore,
		int limitAfter) {
	Expects(IsServerMsgId(key.mergedKey.universalId)
		|| (key.mergedKey.universalId == 0)
		|| (IsServerMsgId(ServerMaxMsgId + key.mergedKey.universalId) && key.mergedKey.migratedPeerId != 0));
	Expects((key.mergedKey.universalId != 0)
		|| (limitBefore == 0 && limitAfter == 0));

	return [=](auto consumer) {
		if (!key.mergedKey.migratedPeerId) {
			return SharedMediaViewer(
				Storage::SharedMediaKey(
					key.mergedKey.peerId,
					key.type,
					SparseIdsMergedSlice::PartKey(key.mergedKey)),
				limitBefore,
				limitAfter
			) | rpl::start_with_next([=](SparseIdsSlice &&part) {
				consumer.put_next(SparseIdsMergedSlice(
					key.mergedKey,
					std::move(part),
					base::none));
			});
		}
		return rpl::combine(
			SharedMediaViewer(
				Storage::SharedMediaKey(
					key.mergedKey.peerId,
					key.type,
					SparseIdsMergedSlice::PartKey(key.mergedKey)),
				limitBefore,
				limitAfter),
			SharedMediaViewer(
				Storage::SharedMediaKey(
					key.mergedKey.migratedPeerId,
					key.type,
					SparseIdsMergedSlice::MigratedKey(key.mergedKey)),
				limitBefore,
				limitAfter)
		) | rpl::start_with_next([=](
				SparseIdsSlice &&part,
				SparseIdsSlice &&migrated) {
			consumer.put_next(SparseIdsMergedSlice(
				key.mergedKey,
				std::move(part),
				std::move(migrated)));
		});
	};
}

SharedMediaWithLastSlice::SharedMediaWithLastSlice(Key key)
: SharedMediaWithLastSlice(
	key,
	SparseIdsMergedSlice(ViewerKey(key)),
	EndingSlice(key)) {
}

SharedMediaWithLastSlice::SharedMediaWithLastSlice(
	Key key,
	SparseIdsMergedSlice slice,
	base::optional<SparseIdsMergedSlice> ending)
: _key(key)
, _slice(std::move(slice))
, _ending(std::move(ending))
, _lastPhotoId(LastPeerPhotoId(key.peerId))
, _isolatedLastPhoto(_key.type == Type::ChatPhoto
	? IsLastIsolated(_slice, _ending, _lastPhotoId)
	: false) {
}

base::optional<int> SharedMediaWithLastSlice::fullCount() const {
	return Add(
		_slice.fullCount(),
		_isolatedLastPhoto | [](bool isolated) { return isolated ? 1 : 0; });
}

base::optional<int> SharedMediaWithLastSlice::skippedBefore() const {
	return _slice.skippedBefore();
}

base::optional<int> SharedMediaWithLastSlice::skippedAfter() const {
	return isolatedInSlice()
		? Add(
			_slice.skippedAfter(),
			lastPhotoSkip())
		: (lastPhotoSkip() | [](int) { return 0; });
}

base::optional<int> SharedMediaWithLastSlice::indexOf(Value value) const {
	return base::get_if<FullMsgId>(&value)
		? _slice.indexOf(*base::get_if<FullMsgId>(&value))
		: (isolatedInSlice()
			|| (*base::get_if<not_null<PhotoData*>>(&value))->id != _lastPhotoId)
			? base::none
			: Add(_slice.size() - 1, lastPhotoSkip());
}

int SharedMediaWithLastSlice::size() const {
	return _slice.size()
		+ ((!isolatedInSlice() && lastPhotoSkip() == 1) ? 1 : 0);
}

SharedMediaWithLastSlice::Value SharedMediaWithLastSlice::operator[](int index) const {
	Expects(index >= 0 && index < size());

	return (index < _slice.size())
		? Value(_slice[index])
		: Value(App::photo(_lastPhotoId));
}

base::optional<int> SharedMediaWithLastSlice::distance(
		const Key &a,
		const Key &b) const {
	if (auto i = indexOf(ComputeId(a))) {
		if (auto j = indexOf(ComputeId(b))) {
			return *j - *i;
		}
	}
	return base::none;
}

PhotoId SharedMediaWithLastSlice::LastPeerPhotoId(PeerId peerId) {
	if (auto peer = App::peerLoaded(peerId)) {
		return peer->photoId;
	}
	return UnknownPeerPhotoId;
}

base::optional<bool> SharedMediaWithLastSlice::IsLastIsolated(
		const SparseIdsMergedSlice &slice,
		const base::optional<SparseIdsMergedSlice> &ending,
		PhotoId lastPeerPhotoId) {
	if (lastPeerPhotoId == UnknownPeerPhotoId) {
		return base::none;
	} else if (!lastPeerPhotoId) {
		return false;
	}
	return LastFullMsgId(ending ? *ending : slice)
		| [](FullMsgId msgId) {	return App::histItemById(msgId); }
		| [](HistoryItem *item) { return item ? item->getMedia() : nullptr; }
		| [](HistoryMedia *media) {
			return (media && media->type() == MediaTypePhoto)
				? static_cast<HistoryPhoto*>(media)->photo()
				: nullptr;
		}
		| [](PhotoData *photo) { return photo ? photo->id : 0; }
		| [&](PhotoId photoId) { return lastPeerPhotoId != photoId; };
}

base::optional<FullMsgId> SharedMediaWithLastSlice::LastFullMsgId(
		const SparseIdsMergedSlice &slice) {
	if (slice.fullCount() == 0) {
		return FullMsgId();
	} else if (slice.size() == 0 || slice.skippedAfter() != 0) {
		return base::none;
	}
	return slice[slice.size() - 1];
}

rpl::producer<SharedMediaWithLastSlice> SharedMediaWithLastViewer(
		SharedMediaWithLastSlice::Key key,
		int limitBefore,
		int limitAfter) {
	return [=](auto consumer) {
		if (base::get_if<not_null<PhotoData*>>(&key.universalId)) {
			return SharedMediaMergedViewer(
				SharedMediaMergedKey(
					SharedMediaWithLastSlice::ViewerKey(key),
					key.type),
				limitBefore,
				limitAfter
			) | rpl::start_with_next([=](SparseIdsMergedSlice &&update) {
				consumer.put_next(SharedMediaWithLastSlice(
					key,
					std::move(update),
					base::none));
			});
		}
		return rpl::combine(
			SharedMediaMergedViewer(
				SharedMediaMergedKey(
					SharedMediaWithLastSlice::ViewerKey(key),
					key.type),
				limitBefore,
				limitAfter),
			SharedMediaMergedViewer(
				SharedMediaMergedKey(
					SharedMediaWithLastSlice::EndingKey(key),
					key.type),
				1,
				1)
		) | rpl::start_with_next([=](
				SparseIdsMergedSlice &&viewer,
				SparseIdsMergedSlice &&ending) {
			consumer.put_next(SharedMediaWithLastSlice(
				key,
				std::move(viewer),
				std::move(ending)));
		});
	};
}
