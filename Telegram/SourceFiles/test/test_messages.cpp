/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_messages.h"

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "data/data_msg_id.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "test/test_log.h"

namespace Test {
namespace {

constexpr auto kProbeInterval = crl::time(5000);

struct Candidate {
	FullMsgId id;
	QString branch = u"none"_q;
	bool present = false;
	bool fallback = false;
};

struct Probe {
	Candidate candidate;
	MsgId lastMessageId = 0;
	int itemPresent = -1;
	int sameHistory = -1;
	int serverId = -1;
	int fallbackNewer = -1;
	int predicate = -1;
	int diagnosticOut = -1;
};

[[nodiscard]] Candidate SelectCandidate(
		FullMsgId localId,
		FullMsgId serverId,
		HistoryItem *last) {
	if (serverId) {
		return {
			.id = serverId,
			.branch = u"mapped"_q,
			.present = true,
		};
	}
	if (!localId && last) {
		return {
			.id = last->fullId(),
			.branch = u"fallback"_q,
			.present = true,
			.fallback = true,
		};
	}
	return {};
}

void LogProbe(
		const QString &tag,
		FullMsgId localId,
		FullMsgId serverId,
		MsgId highWater,
		crl::time startedAt,
		crl::time now,
		crl::time &lastProbeAt,
		const Probe &probe) {
	if (lastProbeAt && (now - lastProbeAt < kProbeInterval)) {
		return;
	}
	lastProbeAt = now;
	LogRaw(u"WATCH_PROBE: tag=%1 local=%2 server=%3 candidate=%4 last=%5 "
		"branch=%6 highWater=%7 elapsedMs=%8 candidatePresent=%9 "
		"itemPresent=%10 sameHistory=%11 serverId=%12 fallbackNewer=%13 "
		"predicate=%14 out=%15"_q
			.arg(tag)
			.arg(qint64(localId.msg.bare))
			.arg(qint64(serverId.msg.bare))
			.arg(qint64(probe.candidate.id.msg.bare))
			.arg(qint64(probe.lastMessageId.bare))
			.arg(probe.candidate.branch)
			.arg(qint64(highWater.bare))
			.arg(qint64(now - startedAt))
			.arg(probe.candidate.present ? 1 : 0)
			.arg(probe.itemPresent)
			.arg(probe.sameHistory)
			.arg(probe.serverId)
			.arg(probe.fallbackNewer)
			.arg(probe.predicate)
			.arg(probe.diagnosticOut));
}

} // namespace

struct SentMessageWatcher::State {
	void observeNewItem(not_null<HistoryItem*> item);
	void observeIdChange(const Data::Session::IdChange &change);

	base::weak_ptr<History> history;
	MsgId highWater = 0;
	Predicate predicate;
	QString diagnosticTag;
	FullMsgId localId;
	FullMsgId serverId;
	FullMsgId matchedId;
	crl::time startedAt = 0;
	crl::time lastProbeAt = 0;
};

void SentMessageWatcher::State::observeNewItem(
		not_null<HistoryItem*> item) {
	const auto target = history.get();
	if (!target
		|| localId
		|| item->history() != target
		|| !IsClientMsgId(item->id)) {
		return;
	}
	localId = item->fullId();
	LogRaw(u"WATCH_LOCAL: tag=%1 id=%2 out=%3"_q
		.arg(diagnosticTag)
		.arg(qint64(localId.msg.bare))
		.arg(item->out() ? 1 : 0));
}

void SentMessageWatcher::State::observeIdChange(
		const Data::Session::IdChange &change) {
	const auto target = history.get();
	if (!target
		|| !localId
		|| serverId
		|| change.oldId != localId.msg
		|| change.newId.peer != localId.peer
		|| !IsServerMsgId(change.newId.msg)) {
		return;
	}
	serverId = change.newId;
	LogRaw(u"WATCH_SERVER: tag=%1 id=%2 wasLocal=%3"_q
		.arg(diagnosticTag)
		.arg(qint64(serverId.msg.bare))
		.arg(qint64(change.oldId.bare)));
}

SentMessageWatcher::SentMessageWatcher(
		not_null<History*> history,
		MsgId highWater,
		Predicate predicate,
		QString diagnosticTag,
		rpl::lifetime &lifetime)
: _state(std::make_shared<State>(State{
	.history = base::make_weak(history),
	.highWater = highWater,
	.predicate = std::move(predicate),
	.diagnosticTag = std::move(diagnosticTag),
	.startedAt = crl::now(),
})) {
	const auto state = _state;
	history->owner().newItemAdded(
	) | rpl::on_next(
		[state](not_null<HistoryItem*> item) {
			state->observeNewItem(item);
		},
		lifetime);
	history->owner().itemIdChanged(
	) | rpl::on_next(
		[state](const Data::Session::IdChange &change) {
			state->observeIdChange(change);
		},
		lifetime);
	LogRaw(u"WATCH_SUBSCRIBED: tag=%1 highWater=%2"_q
		.arg(state->diagnosticTag)
		.arg(qint64(state->highWater.bare)));
}

HistoryItem *SentMessageWatcher::poll() {
	const auto state = _state;
	auto history = state->history.get();
	if (!history) {
		return nullptr;
	}
	if (state->matchedId) {
		return history->owner().message(state->matchedId);
	}

	const auto last = history->lastMessage();
	auto probe = Probe{
		.candidate = SelectCandidate(
			state->localId,
			state->serverId,
			last),
		.lastMessageId = last ? last->id : MsgId(0),
	};
	auto item = probe.candidate.present
		? history->owner().message(probe.candidate.id)
		: nullptr;
	probe.itemPresent = probe.candidate.present ? (item ? 1 : 0) : -1;
	probe.sameHistory = item
		? ((item->history() == history) ? 1 : 0)
		: -1;
	probe.serverId = probe.candidate.present
		? (IsServerMsgId(probe.candidate.id.msg) ? 1 : 0)
		: -1;
	probe.fallbackNewer = probe.candidate.fallback
		? ((probe.candidate.id.msg > state->highWater) ? 1 : 0)
		: -1;
	if (item && probe.sameHistory == 1 && probe.serverId == 1) {
		const auto testedItem = item;
		const auto predicate = state->predicate(testedItem);
		history = state->history.get();
		if (!history) {
			return nullptr;
		}
		const auto currentLast = history->lastMessage();
		const auto currentCandidate = SelectCandidate(
			state->localId,
			state->serverId,
			currentLast);
		if (currentCandidate.id != probe.candidate.id
			|| currentCandidate.branch != probe.candidate.branch
			|| currentCandidate.present != probe.candidate.present
			|| currentCandidate.fallback != probe.candidate.fallback) {
			return nullptr;
		}
		probe.candidate = currentCandidate;
		probe.lastMessageId = currentLast
			? currentLast->id
			: MsgId(0);
		item = history->owner().message(probe.candidate.id);
		if (item != testedItem) {
			return nullptr;
		}
		probe.predicate = predicate ? 1 : 0;
	}
	probe.diagnosticOut = item ? (item->out() ? 1 : 0) : -1;

	const auto now = crl::now();
	LogProbe(
		state->diagnosticTag,
		state->localId,
		state->serverId,
		state->highWater,
		state->startedAt,
		now,
		state->lastProbeAt,
		probe);
	if (!item
		|| probe.sameHistory != 1
		|| probe.serverId != 1
		|| probe.predicate != 1
		|| (probe.candidate.fallback && probe.fallbackNewer != 1)) {
		return nullptr;
	}

	state->matchedId = probe.candidate.id;
	LogRaw(u"WATCH_MATCH: tag=%1 id=%2 out=%3 highWater=%4 branch=%5 "
		"elapsedMs=%6"_q
			.arg(state->diagnosticTag)
			.arg(qint64(state->matchedId.msg.bare))
			.arg(probe.diagnosticOut)
			.arg(qint64(state->highWater.bare))
			.arg(probe.candidate.branch)
			.arg(qint64(now - state->startedAt)));
	return item;
}

} // namespace Test

#endif // _DEBUG
