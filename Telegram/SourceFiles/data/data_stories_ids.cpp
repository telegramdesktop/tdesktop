/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_stories_ids.h"

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
		};
		const auto state = lifetime.make_state<State>();

		const auto push = [=] {
			const auto stories = &peer->owner().stories();
			if (!stories->savedCountKnown(peer->id)) {
				return;
			}

			const auto source = stories->source(peer->id);
			const auto saved = stories->saved(peer->id);
			const auto count = stories->savedCount(peer->id);
			Assert(saved != nullptr);
			auto ids = base::flat_set<StoryId>();
			ids.reserve(saved->list.size() + 1);
			auto total = count;
			if (source && !source->ids.empty()) {
				++total;
				const auto current = source->ids.front().id;
				for (const auto id : ranges::views::reverse(saved->list)) {
					const auto i = source->ids.lower_bound(
						StoryIdDates{ id });
					if (i != end(source->ids) && i->id == id) {
						--total;
					} else {
						ids.emplace(id);
					}
				}
				ids.emplace(current);
			} else {
				auto all = saved->list | ranges::views::reverse;
				ids = { begin(all), end(all) };
			}
			const auto added = int(ids.size());
			state->slice = StoriesIdsSlice(
				std::move(ids),
				total,
				0,
				total - added);
			consumer.put_next_copy(state->slice);
		};

		const auto stories = &peer->owner().stories();
		stories->sourceChanged(
		) | rpl::filter(
			rpl::mappers::_1 == peer->id
		) | rpl::start_with_next([=] {
			push();
		}, lifetime);

		stories->savedChanged(
		) | rpl::filter(
			rpl::mappers::_1 == peer->id
		) | rpl::start_with_next([=] {
			push();
		}, lifetime);

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
		};
		const auto state = lifetime.make_state<State>();

		const auto push = [=] {
			const auto stories = &session->data().stories();
			if (!stories->expiredMineCountKnown()) {
				return;
			}

			const auto expired = stories->expiredMine();
			const auto count = stories->expiredMineCount();
			auto ids = base::flat_set<StoryId>();
			ids.reserve(expired.list.size() + 1);
			auto all = expired.list | ranges::views::reverse;
			ids = { begin(all), end(all) };
			const auto added = int(ids.size());
			state->slice = StoriesIdsSlice(
				std::move(ids),
				count,
				0,
				count - added);
			consumer.put_next_copy(state->slice);
		};

		const auto stories = &session->data().stories();
		stories->expiredMineChanged(
		) | rpl::start_with_next([=] {
			push();
		}, lifetime);

		if (!stories->expiredMineCountKnown()) {
			stories->expiredMineLoadMore();
		}

		push();

		return lifetime;
	};
}

} // namespace Data
