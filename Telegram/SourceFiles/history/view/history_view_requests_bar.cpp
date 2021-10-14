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
#include "data/data_changes.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "ui/chat/requests_bar.h"
#include "ui/chat/group_call_userpics.h"
#include "info/profile/info_profile_values.h"
#include "apiwrap.h"
#include "api/api_invite_links.h"

namespace HistoryView {
namespace {

// If less than 10 requests we request userpics each time the count changes.
constexpr auto kRequestExactThreshold = 10;

} // namespace

rpl::producer<Ui::RequestsBarContent> RequestsBarContentByPeer(
		not_null<PeerData*> peer,
		int userpicSize) {
	struct State {
		explicit State(not_null<PeerData*> peer)
		: peer(peer) {
			current.isGroup = !peer->isBroadcast();
		}
		~State() {
			if (requestId) {
				peer->session().api().request(requestId).cancel();
			}
		}

		not_null<PeerData*> peer;
		std::vector<UserpicInRow> userpics;
		std::vector<not_null<UserData*>> users;
		Ui::RequestsBarContent current;
		base::has_weak_ptr guard;
		mtpRequestId requestId = 0;
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

	static const auto RequestExact = [](
			not_null<State*> state,
			int userpicSize,
			Fn<void()> push,
			auto requestExact) {
		if (state->requestId) {
			return;
		}
		using Flag = MTPmessages_GetChatInviteImporters::Flag;
		state->requestId = state->peer->session().api().request(
			MTPmessages_GetChatInviteImporters(
				MTP_flags(Flag::f_requested),
				state->peer->input,
				MTPstring(), // link
				MTPstring(), // q
				MTP_int(0), // offset_date
				MTP_inputUserEmpty(), // offset_user
				MTP_int(kRecentRequestsLimit))
		).done([=](const MTPmessages_ChatInviteImporters &result) {
			state->requestId = 0;

			result.match([&](
				const MTPDmessages_chatInviteImporters &data) {
				const auto count = data.vcount().v;
				const auto changed = (state->current.count != count);
				const auto &importers = data.vimporters().v;
				auto &owner = state->peer->owner();
				state->users = std::vector<not_null<UserData*>>();
				const auto use = std::min(
					importers.size(),
					HistoryView::kRecentRequestsLimit);
				state->users.reserve(use);
				for (auto i = 0; i != use; ++i) {
					importers[i].match([&](
						const MTPDchatInviteImporter &data) {
						state->users.push_back(
							owner.user(data.vuser_id()));
					});
				}
				if (changed) {
					state->current.count = count;
				}
				if (RegenerateUserpics(state, userpicSize) || changed) {
					push();
				}
				if (state->userpics.size() > state->current.count) {
					requestExact(state, userpicSize, push, requestExact);
				}
			});
		}).fail([=](const MTP::Error &error) {
			state->requestId = 0;
		}).send();
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
			const auto was = state->current.count;
			const auto requestUsersNeeded = (was < kRequestExactThreshold)
				|| (count < kRequestExactThreshold);
			state->current.count = count;
			if (requestUsersNeeded) {
				RequestExact(state, userpicSize, pushNext, RequestExact);
			}
			pushNext();
		}, lifetime);

		using RecentRequests = Api::InviteLinks::RecentRequests;
		api->inviteLinks().recentRequestsLocal(
		) | rpl::filter([=](const RecentRequests &value) {
			return (value.peer == peer)
				&& (state->current.count >= kRequestExactThreshold)
				&& (value.users.size() == kRecentRequestsLimit);
		}) | rpl::start_with_next([=](RecentRequests &&value) {
			state->users = std::move(value.users);
			if (RegenerateUserpics(state, userpicSize)) {
				pushNext();
			}
		}, lifetime);

		return lifetime;
	};
}

} // namespace HistoryView
