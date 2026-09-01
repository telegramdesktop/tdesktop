/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_history_messages.h"

#include "apiwrap.h"
#include "data/data_changes.h"
#include "data/data_chat.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_sparse_ids.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"

namespace Data {
namespace {

struct HistoryMessagesViewerState {
	std::optional<SparseIdsMergedSlice> sparse;
	MessagesSlice slice;
	MessagePosition around;
	MsgId queryAroundId = 0;
	base::has_weak_ptr guard;
	bool scheduled = false;
};

struct HistoryMessagesAround {
	MessagePosition target;
	MsgId query = 0;
};

[[nodiscard]] MessagesSlice HistoryBaseMessagesSlice(
		const SparseIdsMergedSlice &slice,
		MsgId queryAroundId) {
	auto result = MessagesSlice();
	result.fullCount = slice.fullCount();
	result.skippedAfter = slice.skippedAfter();
	result.skippedBefore = slice.skippedBefore();
	const auto count = slice.size();
	result.ids.reserve(count);
	if (const auto msgId = slice.nearest(queryAroundId)) {
		result.nearestToAround = *msgId;
	}
	for (auto i = 0; i != count; ++i) {
		result.ids.push_back(slice[i]);
	}
	return result;
}

[[nodiscard]] std::vector<not_null<HistoryItem*>> HistoryClientSideMessages(
		not_null<History*> history) {
	auto result = std::vector<not_null<HistoryItem*>>();
	for (const auto &item : history->clientSideMessages()) {
		if (item->history() != history) {
			continue;
		}
		result.push_back(item);
	}
	return result;
}

[[nodiscard]] std::optional<MessagePosition> ResolveHistoryAroundPosition(
		not_null<History*> history,
		MessagePosition around,
		FullMsgId fallback) {
	if ((around.fullId.msg == ShowAtTheEndMsgId)
		|| (around == MaxMessagePosition)) {
		return MaxMessagePosition;
	} else if (around.fullId.msg == ShowAtUnreadMsgId) {
		around.fullId = fallback;
	} else if (!around.fullId.msg) {
		around.fullId = fallback;
	}
	if (!around.fullId.msg) {
		return std::nullopt;
	} else if (!around.date) {
		if (const auto item = history->owner().message(around.fullId)) {
			around.date = item->date();
		} else if (fallback && (around.fullId != fallback)) {
			if (const auto item = history->owner().message(fallback)) {
				around = { fallback, item->date() };
			} else {
				return std::nullopt;
			}
		} else {
			return std::nullopt;
		}
	}
	return around;
}

[[nodiscard]] bool HistoryMergedPositionReached(
		FullMsgId candidate,
		TimeId candidateDate,
		MessagePosition target) {
	if (candidate == target.fullId) {
		return true;
	} else if (candidateDate != target.date) {
		return (candidateDate > target.date);
	}
	const auto candidateServer = IsServerMsgId(candidate.msg);
	const auto targetServer = IsServerMsgId(target.fullId.msg);
	if (candidateServer != targetServer) {
		return !candidateServer;
	}
	return (candidate >= target.fullId);
}

void RecomputeHistoryNearestToAround(
		not_null<History*> history,
		not_null<MessagesSlice*> slice,
		MessagePosition around) {
	if (const auto i = ranges::find(slice->ids, around.fullId); i != end(slice->ids)) {
		slice->nearestToAround = *i;
		return;
	}
	const auto fallback = slice->nearestToAround;
	const auto target = ResolveHistoryAroundPosition(history, around, fallback);
	if (!target) {
		slice->nearestToAround = slice->ids.empty() ? FullMsgId() : slice->ids.back();
		return;
	}
	auto nearest = std::optional<FullMsgId>();
	auto last = FullMsgId();
	for (const auto &fullId : slice->ids) {
		const auto item = history->owner().message(fullId);
		if (!item) {
			continue;
		}
		last = fullId;
		if (!nearest && HistoryMergedPositionReached(
				fullId,
				item->date(),
				*target)) {
			nearest = fullId;
		}
	}
	slice->nearestToAround = nearest.value_or(last);
}

void RewriteHistorySliceIds(
		not_null<MessagesSlice*> slice,
		FullMsgId oldId,
		FullMsgId newId) {
	if (slice->nearestToAround == oldId) {
		slice->nearestToAround = newId;
	}
	for (auto &fullId : slice->ids) {
		if (fullId == oldId) {
			fullId = newId;
		}
	}
}

void AppendHistoryClientSideMessages(
		not_null<History*> history,
		MessagePosition around,
		not_null<MessagesSlice*> slice) {
	const auto messages = HistoryClientSideMessages(history);
	if (messages.empty()) {
		RecomputeHistoryNearestToAround(history, slice, around);
		return;
	} else if (slice->ids.empty()) {
		if (slice->skippedBefore != 0 || slice->skippedAfter != 0) {
			return;
		}
	}
	const auto &owner = history->owner();
	auto dates = std::vector<TimeId>();
	dates.reserve(slice->ids.size());
	for (auto i = slice->ids.begin(); i != slice->ids.end();) {
		const auto message = owner.message(*i);
		if (!message) {
			i = slice->ids.erase(i);
			continue;
		}
		dates.push_back(message->date());
		++i;
	}
	for (const auto &item : messages) {
		if (ranges::find(slice->ids, item->fullId()) != end(slice->ids)) {
			continue;
		}
		const auto date = item->date();
		if (dates.empty()) {
			dates.push_back(date);
			slice->ids.push_back(item->fullId());
		} else if (date < dates.front()) {
			if (slice->skippedBefore != 0) {
				if (slice->skippedBefore) {
					++*slice->skippedBefore;
				}
				continue;
			}
			dates.insert(dates.begin(), date);
			slice->ids.insert(slice->ids.begin(), item->fullId());
		} else {
			auto to = dates.size();
			for (; to != 0; --to) {
				const auto checkId = slice->ids[to - 1].msg;
				if (dates[to - 1] > date) {
					continue;
				} else if (dates[to - 1] < date
					|| IsServerMsgId(checkId)
					|| checkId < item->id) {
					break;
				}
			}
			if (item->isSponsored()
				&& (to == dates.size())
				&& (slice->skippedAfter != 0)) {
				continue;
			}
			dates.insert(dates.begin() + to, date);
			slice->ids.insert(slice->ids.begin() + to, item->fullId());
		}
	}
	RecomputeHistoryNearestToAround(history, slice, around);
}

[[nodiscard]] HistoryMessagesAround NormalizeHistoryMessagesAround(
		not_null<History*> history,
		MessagePosition aroundId) {
	const auto computeUnreadAroundId = [&] {
		if (const auto migrated = history->migrateFrom()) {
			if (const auto around = migrated->loadAroundId()) {
				return MsgId(around - ServerMaxMsgId);
			}
		}
		if (const auto around = history->loadAroundId()) {
			return around;
		}
		return MsgId(ServerMaxMsgId - 1);
	};
	auto result = HistoryMessagesAround{
		.target = aroundId,
	};
	if (aroundId.fullId.msg == ShowAtUnreadMsgId) {
		result.query = computeUnreadAroundId();
		const auto migrated = history->peer->migrateFrom();
		const auto peerId = (result.query < 0 && migrated)
			? migrated->id
			: history->peer->id;
		result.target.fullId = FullMsgId(
			peerId,
			(result.query < 0) ? (result.query + ServerMaxMsgId) : result.query);
	} else if ((aroundId.fullId.msg == ShowAtTheEndMsgId)
		|| (aroundId == MaxMessagePosition)) {
		result.query = (ServerMaxMsgId - 1);
		result.target = MaxMessagePosition;
	} else if (aroundId.fullId.msg == 0) {
		result.query = 0;
	} else if (IsClientMsgId(aroundId.fullId.msg)) {
		result.query = (ServerMaxMsgId - 1);
		if (!result.target.date) {
			if (const auto item = history->owner().message(result.target.fullId)) {
				result.target.date = item->date();
			}
		}
	} else {
		const auto samePeer = (aroundId.fullId.peer == history->peer->id);
		if (IsServerMsgId(aroundId.fullId.msg)) {
			result.query = samePeer
				? aroundId.fullId.msg
				: (aroundId.fullId.msg - ServerMaxMsgId);
		} else {
			result.query = (ServerMaxMsgId - 1);
			result.target = MaxMessagePosition;
		}
	}
	return result;
}

} // namespace

void HistoryMessages::addNew(MsgId messageId) {
	_chat.addNew(messageId);
}

std::optional<int> HistoryMessages::countAfter(
		MsgId tillId,
		int limit,
		Fn<bool(MsgId)> counts) const {
	return _chat.countAfter(tillId, limit, std::move(counts));
}

void HistoryMessages::addExisting(MsgId messageId, MsgRange noSkipRange) {
	_chat.addExisting(messageId, noSkipRange);
}

void HistoryMessages::addSlice(
		std::vector<MsgId> &&messageIds,
		MsgRange noSkipRange,
		std::optional<int> count) {
	_chat.addSlice(std::move(messageIds), noSkipRange, count);
}

void HistoryMessages::removeOne(MsgId messageId) {
	_chat.removeOne(messageId);
	_oneRemoved.fire_copy(messageId);
}

void HistoryMessages::removeAll() {
	_chat.removeAll();
	_allRemoved.fire({});
}

void HistoryMessages::invalidateBottom() {
	_chat.invalidateBottom();
	_bottomInvalidated.fire({});
}

Storage::SparseIdsListResult HistoryMessages::snapshot(
		const Storage::SparseIdsListQuery &query) const {
	return _chat.snapshot(query);
}

auto HistoryMessages::sliceUpdated() const
-> rpl::producer<Storage::SparseIdsSliceUpdate> {
	return _chat.sliceUpdated();
}

rpl::producer<MsgId> HistoryMessages::oneRemoved() const {
	return _oneRemoved.events();
}

rpl::producer<> HistoryMessages::allRemoved() const {
	return _allRemoved.events();
}

rpl::producer<> HistoryMessages::bottomInvalidated() const {
	return _bottomInvalidated.events();
}

rpl::producer<SparseIdsSlice> HistoryViewer(
		not_null<History*> history,
		MsgId aroundId,
		int limitBefore,
		int limitAfter) {
	Expects(IsServerMsgId(aroundId) || (aroundId == 0));
	Expects((aroundId != 0) || (limitBefore == 0 && limitAfter == 0));

	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		const auto messages = &history->messages();

		auto builder = lifetime.make_state<SparseIdsSliceBuilder>(
			aroundId,
			limitBefore,
			limitAfter);
		using RequestAroundInfo = SparseIdsSliceBuilder::AroundData;
		builder->insufficientAround(
		) | rpl::on_next([=](const RequestAroundInfo &info) {
			if (!info.aroundId) {
				// Ignore messages-count-only requests, because we perform
				// them with non-zero limit of messages and end up adding
				// a broken slice with several last messages from the chat
				// with a non-skip range starting at zero.
				return;
			}
			history->session().api().requestHistory(
				history,
				info.aroundId,
				info.direction);
		}, lifetime);

		auto pushNextSnapshot = [=] {
			consumer.put_next(builder->snapshot());
		};

		using SliceUpdate = Storage::SparseIdsSliceUpdate;
		messages->sliceUpdated(
		) | rpl::filter([=](const SliceUpdate &update) {
			return builder->applyUpdate(update);
		}) | rpl::on_next(pushNextSnapshot, lifetime);

		messages->oneRemoved(
		) | rpl::filter([=](MsgId messageId) {
			return builder->removeOne(messageId);
		}) | rpl::on_next(pushNextSnapshot, lifetime);

		messages->allRemoved(
		) | rpl::filter([=] {
			return builder->removeAll();
		}) | rpl::on_next(pushNextSnapshot, lifetime);

		messages->bottomInvalidated(
		) | rpl::filter([=] {
			return builder->invalidateBottom();
		}) | rpl::on_next(pushNextSnapshot, lifetime);

		const auto snapshot = messages->snapshot({
			aroundId,
			limitBefore,
			limitAfter,
		});
		if (snapshot.count || !snapshot.messageIds.empty()) {
			if (builder->applyInitial(snapshot)) {
				pushNextSnapshot();
			}
		}
		builder->checkInsufficient();

		return lifetime;
	};
}

rpl::producer<SparseIdsMergedSlice> HistoryMergedViewer(
		not_null<History*> history,
		/*Universal*/MsgId universalAroundId,
		int limitBefore,
		int limitAfter) {
	const auto migrateFrom = history->peer->migrateFrom();
	auto createSimpleViewer = [=](
			PeerId peerId,
			MsgId topicRootId,
			PeerId monoforumPeerId,
			SparseIdsSlice::Key simpleKey,
			int limitBefore,
			int limitAfter) {
		const auto chosen = (history->peer->id == peerId)
			? history
			: history->owner().history(peerId);
		return HistoryViewer(chosen, simpleKey, limitBefore, limitAfter);
	};
	const auto peerId = history->peer->id;
	const auto migratedPeerId = migrateFrom ? migrateFrom->id : PeerId(0);
	using Key = SparseIdsMergedSlice::Key;
	return SparseIdsMergedSlice::CreateViewer(
		Key(peerId, MsgId(), PeerId(), migratedPeerId, universalAroundId),
		limitBefore,
		limitAfter,
		std::move(createSimpleViewer));
}

rpl::producer<MessagesSlice> HistoryMessagesViewer(
		not_null<History*> history,
		MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	const auto around = NormalizeHistoryMessagesAround(history, aroundId);
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();
		const auto viewer = lifetime.make_state<HistoryMessagesViewerState>();
		const auto push = [=] {
			if (!viewer->scheduled || !viewer->sparse) {
				viewer->scheduled = false;
				return;
			}
			viewer->scheduled = false;
			viewer->slice = HistoryBaseMessagesSlice(
				*viewer->sparse,
				viewer->queryAroundId);
			AppendHistoryClientSideMessages(
				history,
				viewer->around,
				&viewer->slice);
			const auto unresolvedCountOnly = viewer->slice.ids.empty()
				&& !viewer->slice.skippedBefore
				&& !viewer->slice.skippedAfter
				&& (viewer->slice.fullCount.value_or(0) > 0);
			if (unresolvedCountOnly) {
				return;
			}
			consumer.put_next_copy(viewer->slice);
		};
		const auto pushInstant = [=] {
			viewer->scheduled = true;
			push();
		};
		const auto pushDelayed = [=] {
			if (!viewer->scheduled) {
				viewer->scheduled = true;
				crl::on_main(&viewer->guard, push);
			}
		};
		viewer->around = around.target;
		viewer->queryAroundId = around.query;

		HistoryMergedViewer(
			history,
			around.query,
			limitBefore,
			limitAfter
		) | rpl::on_next([=](SparseIdsMergedSlice &&slice) {
			viewer->sparse = std::move(slice);
			pushInstant();
		}, lifetime);

		history->session().changes().historyUpdates(
			history,
			HistoryUpdate::Flag::ClientSideMessages
		) | rpl::on_next(pushDelayed, lifetime);

		history->session().data().itemIdChanged(
		) | rpl::filter([=](const Session::IdChange &event) {
			return (event.newId.peer == history->peer->id);
		}) | rpl::on_next([=](const Session::IdChange &event) {
			const auto oldId = FullMsgId(event.newId.peer, event.oldId);
			if (viewer->around.fullId == oldId) {
				viewer->around.fullId = event.newId;
			}
			RewriteHistorySliceIds(&viewer->slice, oldId, event.newId);
		}, lifetime);

		return lifetime;
	};
}

} // namespace Data
