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

			const auto stories = &peer->owner().stories();
			if (!stories->savedCountKnown(peer->id)) {
				return;
			}

			const auto saved = stories->saved(peer->id);
			const auto count = stories->savedCount(peer->id);
			const auto around = saved->list.lower_bound(aroundId);
			Assert(saved != nullptr);
			const auto source = stories->source(peer->id);
			if (!source || source->ids.empty()) {
				const auto hasBefore = int(around - begin(saved->list));
				const auto hasAfter = int(end(saved->list) - around);
				if (hasAfter < limit) {
					stories->savedLoadMore(peer->id);
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
			} else {
				auto ids = base::flat_set<StoryId>();
				auto added = 0;
				auto skipped = 0;
				auto skippedBefore = (around - begin(saved->list));
				auto skippedAfter = (end(saved->list) - around);
				const auto &active = source->ids;
				const auto process = [&](StoryId id) {
					const auto i = active.lower_bound(StoryIdDates{ id });
					if (i == end(active) || i->id != id) {
						ids.emplace(id);
						++added;
					} else {
						++skipped;
					}
					return (added < limit);
				};
				ids.reserve(2 * limit + 1);
				for (auto i = around, b = begin(saved->list); i != b;) {
					--skippedBefore;
					if (!process(*--i)) {
						break;
					}
				}
				if (ids.size() < limit) {
					ids.emplace(active.back().id); // #TODO stories fake max story id
				} else {
					++skippedBefore;
				}
				added = 0;
				for (auto i = around, e = end(saved->list); i != e; ++i) {
					--skippedAfter;
					if (!process(*i)) {
						break;
					}
				}
				state->slice = StoriesIdsSlice(
					std::move(ids),
					count - skipped + 1,
					skippedBefore,
					skippedAfter);
			}
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

		const auto stories = &peer->owner().stories();
		stories->sourceChanged(
		) | rpl::filter(
			rpl::mappers::_1 == peer->id
		) | rpl::start_with_next(schedule, lifetime);

		stories->savedChanged(
		) | rpl::filter(
			rpl::mappers::_1 == peer->id
		) | rpl::start_with_next(schedule, lifetime);

		if (!stories->savedCountKnown(peer->id)) {
			stories->savedLoadMore(peer->id);
		}

		push();

		return lifetime;
	};
}

rpl::producer<StoriesIdsSlice> ArchiveStoriesIds(
		not_null<Main::Session*> session,
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

			const auto stories = &session->data().stories();
			if (!stories->archiveCountKnown()) {
				return;
			}

			const auto &archive = stories->archive();
			const auto count = stories->archiveCount();
			const auto i = archive.list.lower_bound(aroundId);
			const auto hasBefore = int(i - begin(archive.list));
			const auto hasAfter = int(end(archive.list) - i);
			if (hasAfter < limit) {
				stories->archiveLoadMore();
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

		const auto stories = &session->data().stories();
		stories->archiveChanged(
		) | rpl::start_with_next(schedule, lifetime);

		if (!stories->archiveCountKnown()) {
			stories->archiveLoadMore();
		}

		push();

		return lifetime;
	};
}

} // namespace Data
