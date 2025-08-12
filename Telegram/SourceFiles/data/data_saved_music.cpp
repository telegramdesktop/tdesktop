/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_saved_music.h"

#include "api/api_hash.h"
#include "apiwrap.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "ui/ui_utility.h"

namespace Data {
namespace {

constexpr auto kPerPage = 50;

} // namespace

SavedMusic::SavedMusic(not_null<Session*> owner)
: _owner(owner) {
}

bool SavedMusic::Supported(PeerId peerId) {
	return peerId && peerIsUser(peerId);
}

bool SavedMusic::has(not_null<DocumentData*> document) const {
	const auto entry = lookupEntry(_owner->session().userPeerId());
	return entry && ranges::contains(entry->list, document);
}

void SavedMusic::save(not_null<DocumentData*> document) {
	const auto peerId = _owner->session().userPeerId();
	auto &entry = _entries[peerId];
	if (entry.list.empty() && !entry.loaded) {
		loadMore(peerId);
	}
	if (ranges::contains(entry.list, document)) {
		return;
	}
	entry.list.insert(begin(entry.list), document);
	if (entry.total >= 0) {
		++entry.total;
	}
	_owner->session().api().request(MTPaccount_SaveMusic(
		MTP_flags(0),
		document->mtpInput(),
		MTPInputDocument()
	)).send();
	_changed.fire_copy(peerId);
}

void SavedMusic::remove(not_null<DocumentData*> document) {
	const auto peerId = _owner->session().userPeerId();
	auto &entry = _entries[peerId];
	const auto i = ranges::remove(entry.list, document);
	if (const auto removed = int(end(entry.list) - i)) {
		entry.list.erase(i, end(entry.list));
		if (entry.total >= 0) {
			entry.total = std::max(entry.total - removed, 0);
		}
	}
	_owner->session().api().request(MTPaccount_SaveMusic(
		MTP_flags(MTPaccount_SaveMusic::Flag::f_unsave),
		document->mtpInput(),
		MTPInputDocument()
	)).send();
	_changed.fire_copy(peerId);
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

const std::vector<not_null<DocumentData*>> &SavedMusic::list(
		PeerId peerId) const {
	static const auto empty = std::vector<not_null<DocumentData*>>();
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
				if (!ranges::contains(entry.list, document)) {
					entry.list.push_back(document);
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
		DocumentData *aroundId,
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
			auto ids = std::vector<not_null<DocumentData*>>();
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
