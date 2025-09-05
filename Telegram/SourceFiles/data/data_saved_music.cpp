/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_saved_music.h"

#include "api/api_hash.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "main/main_session.h"
#include "ui/ui_utility.h"

namespace Data {
namespace {

constexpr auto kPerPage = 50;
constexpr auto kReloadIdsEvery = 30 * crl::time(1000);

[[nodiscard]] not_null<DocumentData*> ItemDocument(
		not_null<HistoryItem*> item) {
	return item->media()->document();
}

} // namespace

SavedMusic::SavedMusic(not_null<Session*> owner)
: _owner(owner) {
}

SavedMusic::~SavedMusic() {
	Expects(_entries.empty());
}

void SavedMusic::clear() {
	base::take(_entries);
}

bool SavedMusic::Supported(PeerId peerId) {
	return peerId && peerIsUser(peerId);
}

not_null<HistoryItem*> SavedMusic::musicIdToMsg(
		PeerId peerId,
		Entry &entry,
		not_null<DocumentData*> id) {
	const auto i = entry.musicIdToMsg.find(id);
	if (i != end(entry.musicIdToMsg)) {
		return i->second.get();
	} else if (!entry.history) {
		entry.history = _owner->history(peerId);
	}
	return entry.musicIdToMsg.emplace(id, entry.history->makeMessage({
		.id = entry.history->nextNonHistoryEntryId(),
		.flags = (MessageFlag::FakeHistoryItem
			| MessageFlag::HasFromId
			| MessageFlag::SavedMusicItem),
		.from = entry.history->peer->id,
		.date = base::unixtime::now(),
	}, id, TextWithEntities())).first->second.get();
}

void SavedMusic::loadIds() {
	if (_loadIdsRequest
		|| (_lastReceived
			&& (crl::now() - _lastReceived < kReloadIdsEvery))) {
		return;
	}
	_loadIdsRequest = _owner->session().api().request(
		MTPaccount_GetSavedMusicIds(MTP_long(Api::CountHash(_myIds)))
	).done([=](const MTPaccount_SavedMusicIds &result) {
		_loadIdsRequest = 0;
		_lastReceived = crl::now();
		result.match([&](const MTPDaccount_savedMusicIds &data) {
			_myIds = data.vids().v
				| ranges::views::transform(&MTPlong::v)
				| ranges::to_vector;
		}, [](const MTPDaccount_savedMusicIdsNotModified &) {
		});
	}).fail([=] {
		_loadIdsRequest = 0;
		_lastReceived = crl::now();
	}).send();
}

bool SavedMusic::has(not_null<DocumentData*> document) const {
	return ranges::contains(_myIds, document->id);
}

void SavedMusic::save(
		not_null<DocumentData*> document,
		Data::FileOrigin origin) {
	const auto peerId = _owner->session().userPeerId();
	auto &entry = _entries[peerId];
	if (entry.list.empty() && !entry.loaded) {
		loadMore(peerId);
	}
	if (has(document)) {
		return;
	}
	const auto item = musicIdToMsg(peerId, entry, document);
	entry.list.insert(begin(entry.list), item);
	if (entry.total >= 0) {
		++entry.total;
	}
	_myIds.insert(begin(_myIds), document->id);

	const auto send = [=](auto resend) -> void {
		const auto usedFileReference = document->fileReference();
		_owner->session().api().request(MTPaccount_SaveMusic(
			MTP_flags(0),
			document->mtpInput(),
			MTPInputDocument()
		)).fail([=](const MTP::Error &error) {
			if (error.code() == 400
				&& error.type().startsWith(u"FILE_REFERENCE_"_q)) {
				document->session().api().refreshFileReference(origin, [=](
						const auto &) {
					if (document->fileReference() != usedFileReference) {
						resend(resend);
					}
				});
			}
		}).send();
	};
	send(send);

	_changed.fire_copy(peerId);
}

void SavedMusic::remove(not_null<DocumentData*> document) {
	const auto peerId = _owner->session().userPeerId();
	auto &entry = _entries[peerId];
	const auto i = ranges::find(entry.list, document, ItemDocument);
	if (i != end(entry.list)) {
		entry.musicIdFromMsgId.remove((*i)->id);
		entry.list.erase(i);
		if (entry.total > 0) {
			entry.total = std::max(entry.total - 1, 0);
		}
	}
	entry.musicIdToMsg.remove(document);
	_myIds.erase(ranges::remove(_myIds, document->id), end(_myIds));
	_owner->session().api().request(MTPaccount_SaveMusic(
		MTP_flags(MTPaccount_SaveMusic::Flag::f_unsave),
		document->mtpInput(),
		MTPInputDocument()
	)).send();
	_changed.fire_copy(peerId);
}

void SavedMusic::apply(not_null<UserData*> user, const MTPDocument *last) {
	const auto peerId = user->id;
	auto &entry = _entries[peerId];
	if (!last) {
		if (const auto requestId = base::take(entry.requestId)) {
			_owner->session().api().request(requestId).cancel();
		}
		entry = Entry{ .total = 0, .loaded = true };
		_changed.fire_copy(peerId);
		return;
	}
	const auto document = _owner->processDocument(*last);
	const auto i = ranges::find(entry.list, document, ItemDocument);
	if (i != end(entry.list)) {
		if (i == begin(entry.list)) {
			return;
		}
		ranges::rotate(begin(entry.list), i, i + 1);
		_changed.fire_copy(peerId);
		loadMore(peerId, true);
		return;
	}
	entry.list.insert(
		begin(entry.list),
		musicIdToMsg(peerId, entry, document));
	_changed.fire_copy(peerId);
	if (entry.loaded) {
		loadMore(peerId, true);
	}
}

bool SavedMusic::countKnown(PeerId peerId) const {
	if (!Supported(peerId)) {
		return true;
	}
	const auto entry = lookupEntry(peerId);
	return entry && entry->total >= 0;
}

int SavedMusic::count(PeerId peerId) const {
	if (!Supported(peerId)) {
		return 0;
	}
	const auto entry = lookupEntry(peerId);
	return entry ? std::max(entry->total, 0) : 0;
}

const std::vector<not_null<HistoryItem*>> &SavedMusic::list(
		PeerId peerId) const {
	static const auto empty = std::vector<not_null<HistoryItem*>>();
	if (!Supported(peerId)) {
		return empty;
	}

	const auto entry = lookupEntry(peerId);
	return entry ? entry->list : empty;
}

void SavedMusic::loadMore(PeerId peerId) {
	loadMore(peerId, false);
}

void SavedMusic::loadMore(PeerId peerId, bool reload) {
	if (!Supported(peerId)) {
		return;
	}

	auto &entry = _entries[peerId];
	if (!entry.reloading && reload) {
		_owner->session().api().request(
			base::take(entry.requestId)).cancel();
	}
	if ((!reload && entry.loaded) || entry.requestId) {
		return;
	}
	const auto user = _owner->peer(peerId)->asUser();
	Assert(user != nullptr);

	entry.reloading = reload;
	entry.requestId = _owner->session().api().request(MTPusers_GetSavedMusic(
		user->inputUser,
		MTP_int(reload ? 0 : entry.list.size()),
		MTP_int(kPerPage),
		MTP_long(reload ? firstPageHash(entry) : 0)
	)).done([=](const MTPusers_SavedMusic &result) {
		auto &entry = _entries[peerId];
		entry.requestId = 0;
		const auto reloaded = base::take(entry.reloading);
		result.match([&](const MTPDusers_savedMusicNotModified &) {
		}, [&](const MTPDusers_savedMusic &data) {
			const auto list = data.vdocuments().v;
			const auto count = int(list.size());
			entry.total = std::max(count, data.vcount().v);
			if (reloaded) {
				entry.list.clear();
			}
			for (const auto &item : list) {
				const auto document = _owner->processDocument(item);
				if (!ranges::contains(entry.list, document, ItemDocument)) {
					entry.list.push_back(
						musicIdToMsg(peerId, entry, document));
				}
			}
			entry.loaded = list.empty() || (count == entry.list.size());
		});
		_changed.fire_copy(peerId);
	}).fail([=](const MTP::Error &error) {
		auto &entry = _entries[peerId];
		entry.requestId = 0;
		entry.total = int(entry.list.size());
		entry.loaded = true;
		_changed.fire_copy(peerId);
	}).send();
}

uint64 SavedMusic::firstPageHash(const Entry &entry) const {
	return Api::CountHash(entry.list
		| ranges::views::transform(ItemDocument)
		| ranges::views::transform(&DocumentData::id)
		| ranges::views::take(kPerPage));
}

rpl::producer<PeerId> SavedMusic::changed() const {
	return _changed.events();
}

SavedMusic::Entry *SavedMusic::lookupEntry(PeerId peerId) {
	if (!Supported(peerId)) {
		return nullptr;
	}

	auto it = _entries.find(peerId);
	if (it == end(_entries)) {
		return nullptr;
	}
	return &it->second;
}

const SavedMusic::Entry *SavedMusic::lookupEntry(PeerId peerId) const {
	return const_cast<SavedMusic*>(this)->lookupEntry(peerId);
}

rpl::producer<SavedMusicSlice> SavedMusicList(
		not_null<PeerData*> peer,
		HistoryItem *aroundId,
		int limit) {
	if (!peer->isUser()) {
		return rpl::single(SavedMusicSlice({}, 0, 0, 0));
	}
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		struct State {
			SavedMusicSlice slice;
			base::has_weak_ptr guard;
			bool scheduled = false;
		};
		const auto state = lifetime.make_state<State>();

		const auto push = [=] {
			state->scheduled = false;

			const auto peerId = peer->id;
			const auto savedMusic = &peer->owner().savedMusic();
			if (!savedMusic->countKnown(peerId)) {
				return;
			}
			const auto &loaded = savedMusic->list(peerId);
			const auto count = savedMusic->count(peerId);
			auto i = aroundId
				? ranges::find(loaded, not_null(aroundId))
				: begin(loaded);
			if (i == end(loaded)) {
				i = begin(loaded);
			}
			const auto hasBefore = int(i - begin(loaded));
			const auto hasAfter = int(end(loaded) - i);
			if (hasAfter < limit) {
				savedMusic->loadMore(peerId);
			}
			const auto takeBefore = std::min(hasBefore, limit);
			const auto takeAfter = std::min(hasAfter, limit);
			auto ids = std::vector<not_null<HistoryItem*>>();
			ids.reserve(takeBefore + takeAfter);
			for (auto j = i - takeBefore; j != i + takeAfter; ++j) {
				ids.push_back(*j);
			}
			const auto added = int(ids.size());
			state->slice = SavedMusicSlice(
				std::move(ids),
				count,
				(hasBefore - takeBefore),
				count - hasBefore - added);
			consumer.put_next_copy(state->slice);
		};
		const auto schedule = [=] {
			if (state->scheduled) {
				return;
			}
			state->scheduled = true;
			Ui::PostponeCall(&state->guard, [=] {
				if (state->scheduled) {
					push();
				}
			});
		};

		const auto peerId = peer->id;
		const auto savedMusic = &peer->owner().savedMusic();
		savedMusic->changed(
		) | rpl::filter(
			rpl::mappers::_1 == peerId
		) | rpl::start_with_next(schedule, lifetime);

		if (!savedMusic->countKnown(peerId)) {
			savedMusic->loadMore(peerId);
		}

		push();

		return lifetime;
	};
}

} // namespace Data
