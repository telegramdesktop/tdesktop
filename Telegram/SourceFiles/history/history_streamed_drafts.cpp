/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_streamed_drafts.h"

#include "api/api_text_entities.h"
#include "data/data_forum_topic.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"

namespace {

constexpr auto kClearTimeout = 30 * crl::time(1000);

} // namespace

HistoryStreamedDrafts::HistoryStreamedDrafts(not_null<History*> history)
: _history(history)
, _checkTimer([=] { check(); }) {
}

HistoryStreamedDrafts::~HistoryStreamedDrafts() {
	for (const auto &[rootId, draft] : base::take(_drafts)) {
		draft.message->destroy();
	}
}

void HistoryStreamedDrafts::apply(
		MsgId rootId,
		PeerId fromId,
		TimeId when,
		const MTPDsendMessageTextDraftAction &data) {
	if (!rootId) {
		rootId = Data::ForumTopic::kGeneralId;
	}
	if (!when) {
		clear(rootId);
		return;
	}
	const auto text = Api::ParseTextWithEntities(
		&_history->session(),
		data.vtext());
	const auto randomId = data.vrandom_id().v;
	if (update(rootId, randomId, text)) {
		return;
	}
	clear(rootId);
	_drafts.emplace(rootId, Draft{
		.message = _history->addNewLocalMessage({
			.id = _history->owner().nextLocalMessageId(),
			.flags = MessageFlag::Local | MessageFlag::HasReplyInfo,
			.from = fromId,
			.replyTo = {
				.messageId = { _history->peer->id, rootId },
				.topicRootId = rootId,
			},
			.date = when,
		}, text, MTP_messageMediaEmpty()),
		.randomId = randomId,
		.updated = crl::now(),
	});
	if (!_checkTimer.isActive()) {
		_checkTimer.callOnce(kClearTimeout);
	}
}

bool HistoryStreamedDrafts::update(
		MsgId rootId,
		uint64 randomId,
		const TextWithEntities &text) {
	const auto i = _drafts.find(rootId);
	if (i == end(_drafts) || i->second.randomId != randomId) {
		return false;
	}
	i->second.message->setText(text);
	i->second.updated = crl::now();
	return true;
}

void HistoryStreamedDrafts::clear(MsgId rootId) {
	const auto i = _drafts.find(rootId);
	if (i != end(_drafts)) {
		i->second.message->destroy();
		_drafts.erase(i);
	}
	if (_drafts.empty()) {
		scheduleDestroy();
	}
}

void HistoryStreamedDrafts::applyItemAdded(not_null<HistoryItem*> item) {
	const auto rootId = item->topicRootId();
	const auto i = _drafts.find(rootId);
	if (i == end(_drafts) || i->second.message->from() != item->from()) {
		return;
	}
	clear(rootId);
}

void HistoryStreamedDrafts::check() {
	auto closest = crl::time();
	const auto now = crl::now();
	for (auto i = begin(_drafts); i != end(_drafts);) {
		if (now - i->second.updated >= kClearTimeout) {
			i->second.message->destroy();
			i = _drafts.erase(i);
		} else {
			if (!closest || closest > i->second.updated) {
				closest = i->second.updated;
			}
			++i;
		}
	}
	if (closest) {
		_checkTimer.callOnce(kClearTimeout - (now - closest));
	} else {
		scheduleDestroy();
	}
}

void HistoryStreamedDrafts::scheduleDestroy() {
	Expects(_drafts.empty());

	crl::on_main(this, [=] {
		if (_drafts.empty()) {
			_destroyRequests.fire({});
		}
	});
}
