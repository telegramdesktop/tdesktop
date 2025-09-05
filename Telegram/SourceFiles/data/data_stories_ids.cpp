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
#include "ui/ui_utility.h"

namespace Data {

rpl::producer<StoriesIdsSlice> AlbumStoriesIds(
		not_null<PeerData*> peer,
		int albumId,
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
			if (!stories->albumIdsCountKnown(peerId, albumId)) {
				return;
			}
			const auto &loaded = stories->albumIds(peerId, albumId);
			const auto sorted = RespectingPinned(loaded);
			const auto count = stories->albumIdsCount(peerId, albumId);
			auto i = ranges::find(sorted, aroundId);
			if (i == end(sorted)) {
				i = begin(sorted);
			}
			const auto hasBefore = int(i - begin(sorted));
			const auto hasAfter = int(end(sorted) - i);
			if (hasAfter < limit) {
				stories->albumIdsLoadMore(peerId, albumId);
			}
			const auto takeBefore = std::min(hasBefore, limit);
			const auto takeAfter = std::min(hasAfter, limit);
			auto ids = std::vector<StoryId>();
			ids.reserve(takeBefore + takeAfter);
			for (auto j = i - takeBefore; j != i + takeAfter; ++j) {
				ids.push_back(*j);
			}
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
		stories->albumIdsChanged(
		) | rpl::filter(
			rpl::mappers::_1 == Data::StoryAlbumIdsKey{ peerId, albumId }
		) | rpl::start_with_next(schedule, lifetime);

		if (!stories->albumIdsCountKnown(peerId, albumId)) {
			stories->albumIdsLoadMore(peerId, albumId);
		}

		push();

		return lifetime;
	};
}

} // namespace Data
