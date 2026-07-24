/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_streamed_drafts.h"

#include "api/api_text_entities.h"
#include "chat_helpers/stickers_lottie.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_forum_topic.h"
#include "data/data_peer_id.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "iv/iv_rich_page.h"
#include "main/main_session.h"

namespace {

constexpr auto kClearTimeout = 30 * crl::time(1000);

[[nodiscard]] int CommonPrefixLength(const QString &a, const QString &b) {
	const auto count = std::min(a.size(), b.size());
	auto i = 0;
	while (i < count && a[i] == b[i]) {
		++i;
	}
	return i;
}

} // namespace

HistoryStreamedDrafts::HistoryStreamedDrafts(not_null<History*> history)
: _history(history)
, _checkTimer([=] { check(); }) {
}

HistoryStreamedDrafts::~HistoryStreamedDrafts() {
	for (const auto &[randomId, draft] : base::take(_drafts)) {
		draft.message->destroy();
	}
}

TextWithEntities HistoryStreamedDrafts::loadingEmoji() {
	if (_loadingEmoji.empty()) {
		_loadingEmoji = Data::SingleCustomEmoji(
			ChatHelpers::GenerateLocalTgsSticker(
				&_history->session(),
				u"transcribe_loading"_q,
				true));
	}
	return _loadingEmoji;
}

void HistoryStreamedDrafts::apply(
		MsgId rootId,
		PeerId fromId,
		TimeId when,
		const MTPDsendMessageTextDraftAction &data) {
	const auto randomId = data.vrandom_id().v;
	applyPrepared(rootId, fromId, when, randomId, prepareContent(data));
}

void HistoryStreamedDrafts::apply(
		MsgId rootId,
		PeerId fromId,
		TimeId when,
		const MTPDsendMessageRichMessageDraftAction &data) {
	const auto randomId = data.vrandom_id().v;
	applyPrepared(rootId, fromId, when, randomId, prepareContent(data));
}

HistoryStreamedDrafts::DraftContent HistoryStreamedDrafts::prepareContent(
		const MTPDsendMessageTextDraftAction &data) {
	auto content = DraftContent{
		.text = Api::ParseTextWithEntities(
			&_history->session(),
			data.vtext()),
		.kind = DraftKind::Text,
	};
	content.matchText = content.text.text;
	content.text.append(loadingEmoji());
	return content;
}

HistoryStreamedDrafts::DraftContent HistoryStreamedDrafts::prepareContent(
		const MTPDsendMessageRichMessageDraftAction &data) {
	auto content = DraftContent{
		.richPage = Iv::ParseRichPage(
			&_history->session(),
			data.vrich_message()),
		.kind = DraftKind::Rich,
	};
	content.text = Iv::FlattenRichPageSummary(content.richPage, false);
	content.matchText = content.text.text;
	if (content.text.empty()) {
		content.text.append(loadingEmoji());
	}
	return content;
}

void HistoryStreamedDrafts::applyPrepared(
		MsgId rootId,
		PeerId fromId,
		TimeId when,
		uint64 randomId,
		DraftContent &&content) {
	const auto replyToId = rootId
		? FullMsgId(_history->peer->id, rootId)
		: FullMsgId();
	if (!rootId) {
		rootId = Data::ForumTopic::kGeneralId;
	}
	if (!when) {
		clearByRandomId(randomId);
		return;
	}
	if (_drafts.find(randomId) != end(_drafts)
		&& update(randomId, std::move(content))) {
		return;
	}
	const auto item = _history->addNewLocalMessage({
		.id = _history->owner().nextLocalMessageId(),
		.flags = (MessageFlag::Local
			| MessageFlag::HasReplyInfo
			| MessageFlag::TextAppearing),
		.from = fromId,
		.replyTo = {
			.messageId = replyToId,
			.topicRootId = rootId,
		},
		.date = when,
	}, content.text, MTP_messageMediaEmpty());
	if (content.richPage) {
		item->setRichPage(content.richPage);
		_history->owner().requestItemTextRefresh(item);
	}
	_drafts.emplace(randomId, Draft{
		.message = item,
		.rootId = rootId,
		.fromId = fromId,
		.updated = crl::now(),
		.kind = content.kind,
		.matchText = std::move(content.matchText),
	});
	if (!_checkTimer.isActive()) {
		_checkTimer.callOnce(kClearTimeout);
	}
	crl::on_main(this, [=] {
		crl::on_main(this, [=] {
			// Thread topics create views for messages in double on_main:
			// - First we postpone HistoryUpdate::Flag::ClientSideMessages.
			// - Then we postpone RepliesList push of new messages list.
			const auto i = _drafts.find(randomId);
			if (i != end(_drafts)) {
				i->second.message->markTextAppearingStarted();
			}
		});
	});
}

bool HistoryStreamedDrafts::update(
		uint64 randomId,
		DraftContent &&content) {
	const auto i = _drafts.find(randomId);
	if (i == end(_drafts)) {
		return false;
	}
	const auto item = i->second.message;
	const auto currentRichPage = item->richPage();
	const auto hadRichPage = (currentRichPage != nullptr);
	const auto richPageChanged = content.richPage
		? (currentRichPage != content.richPage)
		: hadRichPage;
	const auto textEmpty = content.text.empty();
	if (content.richPage) {
		item->setRichPage(content.richPage);
	} else {
		item->clearRichPage();
	}
	item->setText(std::move(content.text));
	if (richPageChanged || textEmpty) {
		_history->owner().requestItemTextRefresh(item);
	}
	item->invalidateChatListEntry();
	i->second.kind = content.kind;
	i->second.matchText = std::move(content.matchText);
	i->second.updated = crl::now();
	return true;
}

void HistoryStreamedDrafts::clearByRandomId(uint64 randomId) {
	if (const auto draft = _drafts.take(randomId)) {
		draft->message->destroy();
	}
	if (_drafts.empty()) {
		scheduleDestroy();
	}
}

bool HistoryStreamedDrafts::hasFor(not_null<HistoryItem*> item) const {
	const auto rootId = item->topicRootId();
	const auto from = item->from();
	for (const auto &[randomId, draft] : _drafts) {
		if (draft.rootId == rootId && draft.message->from() == from) {
			return true;
		}
	}
	return false;
}

void HistoryStreamedDrafts::applyItemRemoved(not_null<HistoryItem*> item) {
	for (auto i = begin(_drafts); i != end(_drafts); ++i) {
		if (i->second.message == item) {
			_drafts.erase(i);
			if (_drafts.empty()) {
				scheduleDestroy();
			}
			return;
		}
	}
}

HistoryItem *HistoryStreamedDrafts::adoptIncoming(
		const MTPDmessage &data) {
	if (_drafts.empty()) {
		return nullptr;
	}
	const auto fromId = data.vfrom_id()
		? peerFromMTP(*data.vfrom_id())
		: _history->peer->id;
	auto rootId = MsgId(0);
	if (const auto reply = data.vreply_to()) {
		reply->match([&](const MTPDmessageReplyHeader &d) {
			if (d.is_forum_topic()) {
				rootId = d.vreply_to_top_id().value_or_empty();
				if (!rootId) {
					rootId = d.vreply_to_msg_id().value_or_empty();
				}
			}
		}, [](const MTPDmessageReplyStoryHeader &) {});
	}
	if (!rootId) {
		rootId = Data::ForumTopic::kGeneralId;
	}
	auto incomingKind = DraftKind::Text;
	auto incomingText = qs(data.vmessage());
	if (const auto richMessage = data.vrich_message()) {
		incomingKind = DraftKind::Rich;
		incomingText = Iv::FlattenRichPageSummary(
			Iv::ParseRichPage(&_history->session(), *richMessage)).text;
	}
	auto best = end(_drafts);
	if (incomingKind == DraftKind::Rich && incomingText.isEmpty()) {
		for (auto i = begin(_drafts); i != end(_drafts); ++i) {
			const auto &draft = i->second;
			if (draft.rootId != rootId) {
				continue;
			}
			if (draft.message->from()->id != fromId) {
				continue;
			}
			if (draft.kind != DraftKind::Rich) {
				continue;
			}
			if (best == end(_drafts) || draft.updated > best->second.updated) {
				best = i;
			}
		}
	} else {
		auto bestSameKind = end(_drafts);
		auto bestSameKindPrefix = 0;
		auto bestOtherKind = end(_drafts);
		auto bestOtherKindPrefix = 0;
		auto newestSameThreadRich = end(_drafts);
		for (auto i = begin(_drafts); i != end(_drafts); ++i) {
			const auto &draft = i->second;
			if (draft.rootId != rootId) {
				continue;
			}
			if (draft.message->from()->id != fromId) {
				continue;
			}
			if (incomingKind == DraftKind::Rich
				&& draft.kind == DraftKind::Rich
				&& (newestSameThreadRich == end(_drafts)
					|| draft.updated > newestSameThreadRich->second.updated)) {
				newestSameThreadRich = i;
			}
			const auto prefix = CommonPrefixLength(
				draft.matchText,
				incomingText);
			if (prefix <= 0) {
				continue;
			}
			if (draft.kind == incomingKind) {
				if (prefix > bestSameKindPrefix) {
					bestSameKindPrefix = prefix;
					bestSameKind = i;
				}
			} else if (prefix > bestOtherKindPrefix) {
				bestOtherKindPrefix = prefix;
				bestOtherKind = i;
			}
		}
		best = (bestSameKind != end(_drafts))
			? bestSameKind
			: (bestOtherKind != end(_drafts))
			? bestOtherKind
			: newestSameThreadRich;
	}
	if (best == end(_drafts)) {
		return nullptr;
	}
	const auto item = best->second.message.get();
	_drafts.erase(best);

	item->setRealId(data.vid().v);
	if (const auto topic = item->topic()) {
		topic->applyMaybeLast(item);
	}
	if (const auto sublist = item->savedSublist()) {
		sublist->applyMaybeLast(item);
	}
	_history->owner().updateExistingMessage(data);
	_history->newItemAdded(item, NewAddType::StreamedDraftFinish);

	if (_drafts.empty()) {
		scheduleDestroy();
	}
	return item;
}

void HistoryStreamedDrafts::check() {
	auto closest = crl::time();
	const auto now = crl::now();
	for (auto i = begin(_drafts); i != end(_drafts);) {
		if (now - i->second.updated >= kClearTimeout) {
			const auto message = i->second.message;
			i = _drafts.erase(i);
			message->destroy();
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
