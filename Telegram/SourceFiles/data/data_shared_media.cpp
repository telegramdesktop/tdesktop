/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_shared_media.h"

#include <rpl/combine.h>
#include "auth_session.h"
#include "apiwrap.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"
#include "history/history_media_types.h"
#include "data/data_sparse_ids.h"
#include "info/info_memento.h"
#include "info/info_controller.h"
#include "window/window_controller.h"
#include "mainwindow.h"
#include "core/crash_reports.h"

namespace {

using Type = Storage::SharedMediaType;

} // namespace

base::optional<Storage::SharedMediaType> SharedMediaOverviewType(
		Storage::SharedMediaType type) {
	switch (type) {
	case Type::Photo:
	case Type::Video:
	case Type::MusicFile:
	case Type::File:
	case Type::VoiceFile:
	case Type::Link: return type;
	}
	return base::none;
}

void SharedMediaShowOverview(
		Storage::SharedMediaType type,
		not_null<History*> history) {
	if (SharedMediaOverviewType(type)) {
		App::wnd()->controller()->showSection(Info::Memento(
			history->peer->id,
			Info::Section(type)));
	}
}

bool SharedMediaAllowSearch(Storage::SharedMediaType type) {
	switch (type) {
	case Type::MusicFile:
	case Type::File:
	case Type::Link: return true;
	default: return false;
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
				data.aroundId,
				data.direction);
		};
		builder->insufficientAround(
		) | rpl::start_with_next(requestMediaAround, lifetime);

		auto pushNextSnapshot = [=] {
			consumer.put_next(builder->snapshot());
		};

		using SliceUpdate = Storage::SharedMediaSliceUpdate;
		Auth().storage().sharedMediaSliceUpdated(
		) | rpl::filter([=](const SliceUpdate &update) {
			return (update.peerId == key.peerId)
				&& (update.type == key.type);
		}) | rpl::filter([=](const SliceUpdate &update) {
			return builder->applyUpdate(update.data);
		}) | rpl::start_with_next(pushNextSnapshot, lifetime);

		using OneRemoved = Storage::SharedMediaRemoveOne;
		Auth().storage().sharedMediaOneRemoved(
		) | rpl::filter([=](const OneRemoved &update) {
			return (update.peerId == key.peerId)
				&& update.types.test(key.type);
		}) | rpl::filter([=](const OneRemoved &update) {
			return builder->removeOne(update.messageId);
		}) | rpl::start_with_next(pushNextSnapshot, lifetime);

		using AllRemoved = Storage::SharedMediaRemoveAll;
		Auth().storage().sharedMediaAllRemoved(
		) | rpl::filter([=](const AllRemoved &update) {
			return (update.peerId == key.peerId);
		}) | rpl::filter([=] {
			return builder->removeAll();
		}) | rpl::start_with_next(pushNextSnapshot, lifetime);

		using Result = Storage::SharedMediaResult;
		Auth().storage().query(Storage::SharedMediaQuery(
			key,
			limitBefore,
			limitAfter
		)) | rpl::filter([=](const Result &result) {
			return builder->applyInitial(result);
		}) | rpl::start_with_next_done(
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
	auto createSimpleViewer = [=](
			PeerId peerId,
			SparseIdsSlice::Key simpleKey,
			int limitBefore,
			int limitAfter) {
		return SharedMediaViewer(
			Storage::SharedMediaKey(
				peerId,
				key.type,
				simpleKey),
			limitBefore,
			limitAfter
		);
	};
	return SparseIdsMergedSlice::CreateViewer(
		key.mergedKey,
		limitBefore,
		limitAfter,
		std::move(createSimpleViewer));
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

base::optional<int> SharedMediaWithLastSlice::skippedBeforeImpl() const {
	return _slice.skippedBefore();
}

base::optional<int> SharedMediaWithLastSlice::skippedBefore() const {
	return _reversed ? skippedAfterImpl() : skippedBeforeImpl();
}

base::optional<int> SharedMediaWithLastSlice::skippedAfterImpl() const {
	return isolatedInSlice()
		? Add(
			_slice.skippedAfter(),
			lastPhotoSkip())
		: (lastPhotoSkip() | [](int) { return 0; });
}

base::optional<int> SharedMediaWithLastSlice::skippedAfter() const {
	return _reversed ? skippedBeforeImpl() : skippedAfterImpl();
}

base::optional<int> SharedMediaWithLastSlice::indexOfImpl(Value value) const {
	return base::get_if<FullMsgId>(&value)
		? _slice.indexOf(*base::get_if<FullMsgId>(&value))
		: (isolatedInSlice()
			|| !_lastPhotoId
			|| (*base::get_if<not_null<PhotoData*>>(&value))->id != *_lastPhotoId)
			? base::none
			: Add(_slice.size() - 1, lastPhotoSkip());
}

base::optional<int> SharedMediaWithLastSlice::indexOf(Value value) const {
	const auto result = indexOfImpl(value);
	if (result && (*result < 0 || *result >= size())) {
		// Should not happen.
		auto info = QStringList();
		info.push_back("slice:" + QString::number(_slice.size()));
		info.push_back(_slice.fullCount()
			? QString::number(*_slice.fullCount())
			: QString("-"));
		info.push_back(_slice.skippedBefore()
			? QString::number(*_slice.skippedBefore())
			: QString("-"));
		info.push_back(_slice.skippedAfter()
			? QString::number(*_slice.skippedAfter())
			: QString("-"));
		info.push_back("ending:" + (_ending
			? QString::number(_ending->size())
			: QString("-")));
		info.push_back((_ending && _ending->fullCount())
			? QString::number(*_ending->fullCount())
			: QString("-"));
		info.push_back((_ending && _ending->skippedBefore())
			? QString::number(*_ending->skippedBefore())
			: QString("-"));
		info.push_back((_ending && _ending->skippedAfter())
			? QString::number(*_ending->skippedAfter())
			: QString("-"));
		if (const auto msgId = base::get_if<FullMsgId>(&value)) {
			info.push_back("value:" + QString::number(msgId->channel));
			info.push_back(QString::number(msgId->msg));
			const auto index = _slice.indexOf(*base::get_if<FullMsgId>(&value));
			info.push_back("index:" + (index
				? QString::number(*index)
				: QString("-")));
		} else if (const auto photo = base::get_if<not_null<PhotoData*>>(&value)) {
			info.push_back("value:" + QString::number((*photo)->id));
		} else {
			info.push_back("value:bad");
		}
		info.push_back("isolated:" + QString(Logs::b(isolatedInSlice())));
		info.push_back("last:" + (_lastPhotoId
			? QString::number(*_lastPhotoId)
			: QString("-")));
		info.push_back("isolated_last:" + (_isolatedLastPhoto
			? QString(Logs::b(*_isolatedLastPhoto))
			: QString("-")));
		info.push_back("skip:" + (lastPhotoSkip()
			? QString::number(*lastPhotoSkip())
			: QString("-")));
		CrashReports::SetAnnotation("DebugInfo", info.join(','));
		Unexpected("Result in SharedMediaWithLastSlice::indexOf");
	}
	return _reversed
		? (result | func::negate | func::add(size() - 1))
		: result;
}

int SharedMediaWithLastSlice::size() const {
	return _slice.size()
		+ ((!isolatedInSlice() && lastPhotoSkip() == 1) ? 1 : 0);
}

SharedMediaWithLastSlice::Value SharedMediaWithLastSlice::operator[](int index) const {
	Expects(index >= 0 && index < size());

	if (_reversed) {
		index = size() - index - 1;
	}
	return (index < _slice.size())
		? Value(_slice[index])
		: Value(App::photo(*_lastPhotoId));
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

void SharedMediaWithLastSlice::reverse() {
	_reversed = !_reversed;
}

base::optional<PhotoId> SharedMediaWithLastSlice::LastPeerPhotoId(
		PeerId peerId) {
	if (auto peer = App::peerLoaded(peerId)) {
		return peer->userpicPhotoUnknown()
			? base::none
			: base::make_optional(peer->userpicPhotoId());
	}
	return base::none;
}

base::optional<bool> SharedMediaWithLastSlice::IsLastIsolated(
		const SparseIdsMergedSlice &slice,
		const base::optional<SparseIdsMergedSlice> &ending,
		base::optional<PhotoId> lastPeerPhotoId) {
	if (!lastPeerPhotoId) {
		return base::none;
	} else if (!*lastPeerPhotoId) {
		return false;
	}
	return LastFullMsgId(ending ? *ending : slice)
		| [](FullMsgId msgId) {	return App::histItemById(msgId); }
		| [](HistoryItem *item) { return item ? item->getMedia() : nullptr; }
		| [](HistoryMedia *media) {
			return media ? media->getPhoto() : nullptr;
		}
		| [](PhotoData *photo) { return photo ? photo->id : 0; }
		| [&](PhotoId photoId) { return *lastPeerPhotoId != photoId; };
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

rpl::producer<SharedMediaWithLastSlice> SharedMediaWithLastReversedViewer(
		SharedMediaWithLastSlice::Key key,
		int limitBefore,
		int limitAfter) {
	return SharedMediaWithLastViewer(
		key,
		limitBefore,
		limitAfter
	) | rpl::map([](SharedMediaWithLastSlice &&slice) {
		slice.reverse();
		return std::move(slice);
	});
}
