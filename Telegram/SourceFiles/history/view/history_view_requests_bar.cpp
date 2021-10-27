/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_requests_bar.h"

#include "history/view/history_view_group_call_bar.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "ui/chat/requests_bar.h"
#include "ui/chat/group_call_userpics.h"
#include "info/profile/info_profile_values.h"
#include "apiwrap.h"

namespace HistoryView {

rpl::producer<Ui::RequestsBarContent> RequestsBarContentByPeer(
		not_null<PeerData*> peer,
		int userpicSize) {
	struct State {
		explicit State(not_null<PeerData*> peer)
		: peer(peer) {
			current.isGroup = !peer->isBroadcast();
		}

		not_null<PeerData*> peer;
		std::vector<UserpicInRow> userpics;
		std::vector<not_null<UserData*>> users;
		Ui::RequestsBarContent current;
		base::has_weak_ptr guard;
		bool someUserpicsNotLoaded = false;
		bool pushScheduled = false;
	};

	static const auto FillUserpics = [](
			not_null<State*> state) {
		const auto &users = state->users;
		const auto same = ranges::equal(
			state->userpics,
			users,
			ranges::equal_to(),
			&UserpicInRow::peer);
		if (same) {
			return false;
		}
		for (auto b = begin(users), e = end(users), i = b; i != e; ++i) {
			const auto user = *i;
			const auto j = ranges::find(
				state->userpics,
				user,
				&UserpicInRow::peer);
			const auto place = begin(state->userpics) + (i - b);
			if (j == end(state->userpics)) {
				state->userpics.insert(
					place,
					UserpicInRow{ .peer = user });
			} else if (j > place) {
				ranges::rotate(place, j, j + 1);
			}
		}
		while (state->userpics.size() > users.size()) {
			state->userpics.pop_back();
		}
		return true;
	};

	static const auto RegenerateUserpics = [](
			not_null<State*> state,
			int userpicSize,
			bool force = false) {
		const auto result = FillUserpics(state) || force;
		if (!result) {
			return false;
		}
		state->current.users.reserve(state->userpics.size());
		state->current.users.clear();
		state->someUserpicsNotLoaded = false;
		for (auto &userpic : state->userpics) {
			userpic.peer->loadUserpic();
			const auto pic = userpic.peer->genUserpic(
				userpic.view,
				userpicSize);
			userpic.uniqueKey = userpic.peer->userpicUniqueKey(userpic.view);
			state->current.users.push_back({
				.userpic = pic.toImage(),
				.userpicKey = userpic.uniqueKey,
				.id = userpic.peer->id.value,
			});
			if (userpic.peer->hasUserpic()
				&& userpic.peer->useEmptyUserpic(userpic.view)) {
				state->someUserpicsNotLoaded = true;
			}
		}
		return true;
	};

	return [=](auto consumer) {
		const auto api = &peer->session().api();

		auto lifetime = rpl::lifetime();
		auto state = lifetime.make_state<State>(peer);

		const auto pushNext = [=] {
			if (state->pushScheduled
				|| (std::min(state->current.count, kRecentRequestsLimit)
					!= state->users.size())) {
				return;
			}
			state->pushScheduled = true;
			crl::on_main(&state->guard, [=] {
				state->pushScheduled = false;
				consumer.put_next_copy(state->current);
			});
		};

		peer->session().downloaderTaskFinished(
		) | rpl::filter([=] {
			return state->someUserpicsNotLoaded;
		}) | rpl::start_with_next([=] {
			for (const auto &userpic : state->userpics) {
				if (userpic.peer->userpicUniqueKey(userpic.view)
					!= userpic.uniqueKey) {
					RegenerateUserpics(state, userpicSize, true);
					pushNext();
					return;
				}
			}
		}, lifetime);

		Info::Profile::PendingRequestsCountValue(
			peer
		) | rpl::filter([=](int count) {
			return (state->current.count != count);
		}) | rpl::start_with_next([=](int count) {
			const auto &requesters = peer->isChat()
				? peer->asChat()->recentRequesters()
				: peer->asChannel()->recentRequesters();
			auto &owner = state->peer->owner();
			const auto old = base::take(state->users);
			state->users = std::vector<not_null<UserData*>>();
			const auto use = std::min(
				int(requesters.size()),
				HistoryView::kRecentRequestsLimit);
			state->users.reserve(use);
			for (auto i = 0; i != use; ++i) {
				state->users.push_back(owner.user(requesters[i]));
			}
			const auto changed = (state->current.count != count)
				|| (count == 1
					&& ((state->users.size() != old.size())
						|| (old.size() == 1
							&& state->users.front() != old.front())));
			if (changed) {
				state->current.count = count;
				if (count == 1 && !state->users.empty()) {
					const auto user = state->users.front();
					state->current.nameShort = user->shortName();
					state->current.nameFull = user->name;
				} else {
					state->current.nameShort
						= state->current.nameFull
						= QString();
				}
			}
			if (RegenerateUserpics(state, userpicSize) || changed) {
				pushNext();
			}
		}, lifetime);

		return lifetime;
	};
}

} // namespace HistoryView
