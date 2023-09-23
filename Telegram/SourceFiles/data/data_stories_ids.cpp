/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_stories_ids.h"

#include "data/data_changes.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "main/main_session.h"

namespace Data {

rpl::producer<StoriesIdsSlice> SavedStoriesIds(
		not_null<PeerData*> peer,
		StoryId aroundId,
		int limit) {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		struct State {
			StoriesIdsSlice slice;
			base::has_weak_ptr guard;
			bool scheduled = false;
		};
		const auto state = lifetime.make_state<State>();

		const auto push = [=] {
			state->scheduled = false;

			const auto peerId = peer->id;
			const auto stories = &peer->owner().stories();
			if (!stories->savedCountKnown(peerId)) {
				return;
			}

			const auto &saved = stories->saved(peerId);
			const auto count = stories->savedCount(peerId);
			const auto around = saved.list.lower_bound(aroundId);
			const auto hasBefore = int(around - begin(saved.list));
			const auto hasAfter = int(end(saved.list) - around);
			if (hasAfter < limit) {
				stories->savedLoadMore(peerId);
			}
			const auto takeBefore = std::min(hasBefore, limit);
			const auto takeAfter = std::min(hasAfter, limit);
			auto ids = base::flat_set<StoryId>{
				std::make_reverse_iterator(around + takeAfter),
				std::make_reverse_iterator(around - takeBefore)
			};
			const auto added = int(ids.size());
			state->slice = StoriesIdsSlice(
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
		const auto stories = &peer->owner().stories();
		stories->savedChanged(
		) | rpl::filter(
			rpl::mappers::_1 == peerId
		) | rpl::start_with_next(schedule, lifetime);

		if (!stories->savedCountKnown(peerId)) {
			stories->savedLoadMore(peerId);
		}

		push();

		return lifetime;
	};
}

rpl::producer<StoriesIdsSlice> ArchiveStoriesIds(
		not_null<PeerData*> peer,
		StoryId aroundId,
		int limit) {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		struct State {
			StoriesIdsSlice slice;
			base::has_weak_ptr guard;
			bool scheduled = false;
		};
		const auto state = lifetime.make_state<State>();

		const auto push = [=] {
			state->scheduled = false;

			const auto peerId = peer->id;
			const auto stories = &peer->owner().stories();
			if (!stories->archiveCountKnown(peerId)) {
				return;
			}

			const auto &archive = stories->archive(peerId);
			const auto count = stories->archiveCount(peerId);
			const auto i = archive.list.lower_bound(aroundId);
			const auto hasBefore = int(i - begin(archive.list));
			const auto hasAfter = int(end(archive.list) - i);
			if (hasAfter < limit) {
				stories->archiveLoadMore(peerId);
			}
			const auto takeBefore = std::min(hasBefore, limit);
			const auto takeAfter = std::min(hasAfter, limit);
			auto ids = base::flat_set<StoryId>{
				std::make_reverse_iterator(i + takeAfter),
				std::make_reverse_iterator(i - takeBefore)
			};
			const auto added = int(ids.size());
			state->slice = StoriesIdsSlice(
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
		const auto stories = &peer->owner().stories();
		stories->archiveChanged(
		) | rpl::start_with_next(schedule, lifetime);

		if (!stories->archiveCountKnown(peerId)) {
			stories->archiveLoadMore(peerId);
		}

		push();

		return lifetime;
	};
}

} // namespace Data
