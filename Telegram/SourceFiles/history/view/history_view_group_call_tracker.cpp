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
#include "ui/chat/group_call_userpics.h"
#include "ui/painter.h"
#include "calls/group/calls_group_call.h"
#include "calls/calls_instance.h"
#include "core/application.h"
#include "styles/style_chat.h"

namespace HistoryView {

void GenerateUserpicsInRow(
		QImage &result,
		const std::vector<UserpicInRow> &list,
		const style::GroupCallUserpics &st,
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

GroupCallTracker::GroupCallTracker(not_null<PeerData*> peer)
: _peer(peer) {
}

rpl::producer<Ui::GroupCallBarContent> GroupCallTracker::ContentByCall(
		not_null<Data::GroupCall*> call,
		int userpicSize) {
	struct State {
		std::vector<UserpicInRow> userpics;
		Ui::GroupCallBarContent current;
		base::has_weak_ptr guard;
		bool someUserpicsNotLoaded = false;
		bool scheduled = false;
	};

	// speaking DESC, std::max(date, lastActive) DESC
	static const auto SortKey = [](const Data::GroupCallParticipant &p) {
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
		std::array<const Data::GroupCallParticipant*, kLimit> adding{
			{ nullptr }
		};
		for (const auto &participant : call->participants()) {
			const auto alreadyInList = ranges::contains(
				state->userpics,
				participant.peer,
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
				state->userpics.push_back(UserpicInRow{
					.peer = adding[i]->peer,
					.speaking = adding[i]->speaking,
				});
			}
		}
		return true;
	};

	static const auto RegenerateUserpics = [](
			not_null<State*> state,
			not_null<Data::GroupCall*> call,
			int userpicSize,
			bool force = false) {
		const auto result = FillMissingUserpics(state, call) || force;
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
				.speaking = userpic.speaking,
			});
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
			not_null<PeerData*> participantPeer,
			int userpicSize) {
		const auto i = ranges::find(
			state->userpics,
			participantPeer,
			&UserpicInRow::peer);
		if (i == state->userpics.end()) {
			return false;
		}
		state->userpics.erase(i);
		RegenerateUserpics(state, call, userpicSize, true);
		return true;
	};

	static const auto CheckPushToFront = [](
			not_null<State*> state,
			not_null<Data::GroupCall*> call,
			not_null<PeerData*> participantPeer,
			int userpicSize) {
		Expects(state->userpics.size() <= kLimit);

		const auto &participants = call->participants();
		auto i = begin(state->userpics);

		// Find where to put a new speaking userpic.
		for (; i != end(state->userpics); ++i) {
			if (i->peer == participantPeer) {
				if (i->speaking) {
					return false;
				}
				const auto index = i - begin(state->userpics);
				state->current.users[index].speaking = i->speaking = true;
				return true;
			}
			const auto j = ranges::find(
				participants,
				i->peer,
				&Data::GroupCallParticipant::peer);
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
		const auto added = state->userpics.insert(i, UserpicInRow{
			.peer = participantPeer,
			.speaking = true,
		});

		// Remove him from the tail, if he was there.
		for (auto i = added + 1; i != state->userpics.end(); ++i) {
			if (i->peer == participantPeer) {
				state->userpics.erase(i);
				break;
			}
		}

		if (state->userpics.size() > kLimit) {
			// Find last non-speaking userpic to remove. It must be there.
			for (auto i = state->userpics.end() - 1; i != added; --i) {
				const auto j = ranges::find(
					participants,
					i->peer,
					&Data::GroupCallParticipant::peer);
				if (j == end(participants) || !j->speaking) {
					// Found a non-speaking one, remove.
					state->userpics.erase(i);
					break;
				}
			}
			Assert(state->userpics.size() <= kLimit);
		}
		RegenerateUserpics(state, call, userpicSize, true);
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
			const auto participantPeer = update.now
				? update.now->peer
				: update.was->peer;
			if (!update.now) {
				const auto removed = RemoveUserpic(
					state,
					call,
					participantPeer,
					userpicSize);
				if (removed) {
					pushNext();
				}
			} else if (update.now->speaking
				&& (!update.was || !update.was->speaking)) {
				const auto pushed = CheckPushToFront(
					state,
					call,
					participantPeer,
					userpicSize);
				if (pushed) {
					pushNext();
				}
			} else {
				auto updateSpeakingState = update.was.has_value()
					&& (update.now->speaking != update.was->speaking);
				if (updateSpeakingState) {
					const auto i = ranges::find(
						state->userpics,
						participantPeer,
						&UserpicInRow::peer);
					if (i != end(state->userpics)) {
						const auto index = i - begin(state->userpics);
						state->current.users[index].speaking
							= i->speaking
							= update.now->speaking;
					} else {
						updateSpeakingState = false;
					}
				}
				if (RegenerateUserpics(state, call, userpicSize)
					|| updateSpeakingState) {
					pushNext();
				}
			}
		}, lifetime);

		call->participantsReloaded(
		) | rpl::filter([=] {
			return RegenerateUserpics(state, call, userpicSize);
		}) | rpl::start_with_next(pushNext, lifetime);

		call->peer()->session().downloaderTaskFinished(
		) | rpl::filter([=] {
			return state->someUserpicsNotLoaded;
		}) | rpl::start_with_next([=] {
			for (const auto &userpic : state->userpics) {
				if (userpic.peer->userpicUniqueKey(userpic.view)
					!= userpic.uniqueKey) {
					RegenerateUserpics(state, call, userpicSize, true);
					pushNext();
					return;
				}
			}
		}, lifetime);

		RegenerateUserpics(state, call, userpicSize);

		rpl::combine(
			call->titleValue(),
			call->scheduleDateValue(),
			call->fullCountValue()
		) | rpl::start_with_next([=](
				const QString &title,
				TimeId scheduleDate,
				int count) {
			state->current.title = title;
			state->current.scheduleDate = scheduleDate;
			state->current.count = count;
			state->current.shown = (count > 0) || (scheduleDate != 0);
			consumer.put_next_copy(state->current);
		}, lifetime);

		return lifetime;
	};
}

rpl::producer<Ui::GroupCallBarContent> GroupCallTracker::content() const {
	const auto peer = _peer;
	return rpl::combine(
		peer->session().changes().peerFlagsValue(
			peer,
			Data::PeerUpdate::Flag::GroupCall),
		Core::App().calls().currentGroupCallValue()
	) | rpl::map([=](const auto&, Calls::GroupCall *current) {
		const auto call = peer->groupCall();
		return (call && (!current || current->peer() != peer))
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
		return ContentByCall(call, st::historyGroupCallUserpics.size);
	}) | rpl::flatten_latest();
}

rpl::producer<> GroupCallTracker::joinClicks() const {
	return _joinClicks.events();
}

} // namespace HistoryView
