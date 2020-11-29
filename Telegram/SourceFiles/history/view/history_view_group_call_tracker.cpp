/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_group_call_tracker.h"

#include "data/data_channel.h"
#include "data/data_user.h"
#include "data/data_changes.h"
#include "data/data_group_call.h"
#include "main/main_session.h"
#include "ui/chat/group_call_bar.h"
#include "ui/painter.h"
#include "calls/calls_group_call.h"
#include "calls/calls_instance.h"
#include "core/application.h"
#include "styles/style_chat.h"

namespace HistoryView {

void GenerateUserpicsInRow(
		QImage &result,
		const std::vector<UserpicInRow> &list,
		const UserpicsInRowStyle &st,
		int maxElements) {
	const auto count = int(list.size());
	if (!count) {
		result = QImage();
		return;
	}
	const auto limit = std::max(count, maxElements);
	const auto single = st.size;
	const auto shift = st.shift;
	const auto width = single + (limit - 1) * (single - shift);
	if (result.width() != width * cIntRetinaFactor()) {
		result = QImage(
			QSize(width, single) * cIntRetinaFactor(),
			QImage::Format_ARGB32_Premultiplied);
	}
	result.fill(Qt::transparent);
	result.setDevicePixelRatio(cRetinaFactor());

	auto q = Painter(&result);
	auto hq = PainterHighQualityEnabler(q);
	auto pen = QPen(Qt::transparent);
	pen.setWidth(st.stroke);
	auto x = (count - 1) * (single - shift);
	for (auto i = count; i != 0;) {
		auto &entry = list[--i];
		q.setCompositionMode(QPainter::CompositionMode_SourceOver);
		entry.peer->paintUserpic(q, entry.view, x, 0, single);
		entry.uniqueKey = entry.peer->userpicUniqueKey(entry.view);
		q.setCompositionMode(QPainter::CompositionMode_Source);
		q.setBrush(Qt::NoBrush);
		q.setPen(pen);
		q.drawEllipse(x, 0, single, single);
		x -= single - shift;
	}
}

GroupCallTracker::GroupCallTracker(not_null<ChannelData*> channel)
: _channel(channel) {
}

rpl::producer<Ui::GroupCallBarContent> GroupCallTracker::ContentByCall(
		not_null<Data::GroupCall*> call,
		const UserpicsInRowStyle &st) {
	struct State {
		std::vector<UserpicInRow> userpics;
		Ui::GroupCallBarContent current;
		base::has_weak_ptr guard;
		bool someUserpicsNotLoaded = false;
		bool scheduled = false;
	};

	// speaking DESC, std::max(date, lastActive) DESC
	static const auto SortKey = [](const Data::GroupCall::Participant &p) {
		const auto result = (p.speaking ? uint64(0x100000000ULL) : uint64(0))
			| uint64(std::max(p.lastActive, p.date));
		return (~uint64(0)) - result; // sorting with less(), so invert.
	};

	constexpr auto kLimit = 3;
	static const auto FillMissingUserpics = [](
			not_null<State*> state,
			not_null<Data::GroupCall*> call) {
		const auto already = int(state->userpics.size());
		const auto &participants = call->participants();
		if (already >= kLimit || participants.size() <= already) {
			return false;
		}
		std::array<const Data::GroupCall::Participant*, kLimit> adding{
			{ nullptr }
		};
		for (const auto &participant : call->participants()) {
			const auto alreadyInList = ranges::contains(
				state->userpics,
				participant.user,
				&UserpicInRow::peer);
			if (alreadyInList) {
				continue;
			}
			for (auto i = 0; i != kLimit; ++i) {
				if (!adding[i]) {
					adding[i] = &participant;
					break;
				} else if (SortKey(participant) < SortKey(*adding[i])) {
					for (auto j = kLimit - 1; j != i; --j) {
						adding[j] = adding[j - 1];
					}
					adding[i] = &participant;
					break;
				}
			}
		}
		for (auto i = 0; i != kLimit - already; ++i) {
			if (adding[i]) {
				state->userpics.push_back(UserpicInRow{ adding[i]->user });
			}
		}
		return true;
	};

	static const auto RegenerateUserpics = [](
			not_null<State*> state,
			not_null<Data::GroupCall*> call,
			const UserpicsInRowStyle &st,
			bool force = false) {
		const auto result = FillMissingUserpics(state, call) || force;
		if (!result) {
			return false;
		}
		GenerateUserpicsInRow(
			state->current.userpics,
			state->userpics,
			st);
		state->someUserpicsNotLoaded = false;
		for (const auto &userpic : state->userpics) {
			if (userpic.peer->hasUserpic()
				&& userpic.peer->useEmptyUserpic(userpic.view)) {
				state->someUserpicsNotLoaded = true;
			}
		}
		return true;
	};

	static const auto RemoveUserpic = [](
			not_null<State*> state,
			not_null<Data::GroupCall*> call,
			not_null<UserData*> user,
			const UserpicsInRowStyle &st) {
		const auto i = ranges::find(
			state->userpics,
			user,
			&UserpicInRow::peer);
		if (i == state->userpics.end()) {
			return false;
		}
		state->userpics.erase(i);
		RegenerateUserpics(state, call, st, true);
		return true;
	};

	static const auto CheckPushToFront = [](
			not_null<State*> state,
			not_null<Data::GroupCall*> call,
			not_null<UserData*> user,
			const UserpicsInRowStyle &st) {
		Expects(state->userpics.size() <= kLimit);

		const auto &participants = call->participants();
		auto i = state->userpics.begin();

		// Find where to put a new speaking userpic.
		for (; i != state->userpics.end(); ++i) {
			if (i->peer == user) {
				return false;
			}
			const auto j = ranges::find(
				participants,
				not_null{ static_cast<UserData*>(i->peer.get()) },
				&Data::GroupCall::Participant::user);
			if (j == end(participants) || !j->speaking) {
				// Found a non-speaking one, put the new speaking one here.
				break;
			}
		}
		if (i - state->userpics.begin() >= kLimit) {
			// Full kLimit of speaking userpics already.
			return false;
		}

		// Add the new speaking to the place we found.
		const auto added = state->userpics.insert(i, UserpicInRow{ user });

		// Remove him from the tail, if he was there.
		for (auto i = added + 1; i != state->userpics.end(); ++i) {
			if (i->peer == user) {
				state->userpics.erase(i);
				break;
			}
		}

		if (state->userpics.size() > kLimit) {
			// Find last non-speaking userpic to remove. It must be there.
			for (auto i = state->userpics.end() - 1; i != added; --i) {
				const auto j = ranges::find(
					participants,
					not_null{ static_cast<UserData*>(i->peer.get()) },
					&Data::GroupCall::Participant::user);
				if (j == end(participants) || !j->speaking) {
					// Found a non-speaking one, remove.
					state->userpics.erase(i);
					break;
				}
			}
			Assert(state->userpics.size() <= kLimit);
		}
		RegenerateUserpics(state, call, st, true);
		return true;
	};

	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();
		auto state = lifetime.make_state<State>();
		state->current.shown = true;

		const auto pushNext = [=] {
			if (state->scheduled) {
				return;
			}
			state->scheduled = true;
			crl::on_main(&state->guard, [=] {
				state->scheduled = false;
				consumer.put_next_copy(state->current);
			});
		};

		using ParticipantUpdate = Data::GroupCall::ParticipantUpdate;
		call->participantUpdated(
		) | rpl::start_with_next([=](const ParticipantUpdate &update) {
			const auto user = update.now ? update.now->user : update.was->user;
			if (!update.now) {
				if (RemoveUserpic(state, call, user, st)) {
					pushNext();
				}
			} else if (update.now->speaking
				&& (!update.was || !update.was->speaking)) {
				if (CheckPushToFront(state, call, user, st)) {
					pushNext();
				}
			} else if (RegenerateUserpics(state, call, st)) {
				pushNext();
			}
		}, lifetime);

		call->participantsSliceAdded(
		) | rpl::filter([=] {
			return RegenerateUserpics(state, call, st);
		}) | rpl::start_with_next(pushNext, lifetime);

		call->channel()->session().downloaderTaskFinished(
		) | rpl::filter([=] {
			return state->someUserpicsNotLoaded;
		}) | rpl::start_with_next([=] {
			for (const auto &userpic : state->userpics) {
				if (userpic.peer->userpicUniqueKey(userpic.view)
					!= userpic.uniqueKey) {
					RegenerateUserpics(state, call, st, true);
					pushNext();
					return;
				}
			}
		}, lifetime);

		RegenerateUserpics(state, call, st);

		call->fullCountValue(
		) | rpl::start_with_next([=](int count) {
			state->current.count = count;
			consumer.put_next_copy(state->current);
		}, lifetime);

		return lifetime;
	};
}

rpl::producer<Ui::GroupCallBarContent> GroupCallTracker::content() const {
	const auto channel = _channel;
	return rpl::combine(
		channel->session().changes().peerFlagsValue(
			channel,
			Data::PeerUpdate::Flag::GroupCall),
		Core::App().calls().currentGroupCallValue()
	) | rpl::map([=](const auto&, Calls::GroupCall *current) {
		const auto call = channel->call();
		return (call && (!current || current->channel() != channel))
			? call
			: nullptr;
	}) | rpl::distinct_until_changed(
	) | rpl::map([](Data::GroupCall *call)
	-> rpl::producer<Ui::GroupCallBarContent> {
		if (!call) {
			return rpl::single(Ui::GroupCallBarContent{ .shown = false });
		} else if (!call->fullCount() && !call->participantsLoaded()) {
			call->reload();
		}
		const auto st = UserpicsInRowStyle{
			.size = st::historyGroupCallUserpicSize,
			.shift = st::historyGroupCallUserpicShift,
			.stroke = st::historyGroupCallUserpicStroke,
		};
		return ContentByCall(call, st);
	}) | rpl::flatten_latest();
}

rpl::producer<> GroupCallTracker::joinClicks() const {
	return _joinClicks.events();
}

} // namespace HistoryView
