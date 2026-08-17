/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_history_fixtures.h"

#include "base/unixtime.h"
#include "base/weak_ptr.h"
#include "data/data_msg_id.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "data/data_types.h"
#include "history/history.h"
#include "history/history_item.h"
#include "test/test_log.h"

namespace Test {

void LogServiceMessagePredicates(
		HistoryItem *item,
		const QString &tag) {
	if (!item) {
		LogRaw(u"FIXTURE_SERVICE: tag=%1 item=null"_q.arg(tag));
		return;
	}
	LogRaw(u"FIXTURE_SERVICE: tag=%1 id=%2 clientMsgId=%3 serverMsgId=%4 "
		"isService=%5 isRegular=%6 isHistoryEntry=%7 isLocal=%8 canDelete=%9 "
		"allowsForward=%10 canBeSelected=%11 out=%12 view=%13 "
		"messagesIndex=%14 lastServerMessage=%15"_q
			.arg(tag)
			.arg(qint64(item->id.bare))
			.arg(IsClientMsgId(item->id) ? 1 : 0)
			.arg(IsServerMsgId(item->id) ? 1 : 0)
			.arg(item->isService() ? 1 : 0)
			.arg(item->isRegular() ? 1 : 0)
			.arg(item->isHistoryEntry() ? 1 : 0)
			.arg(item->isLocal() ? 1 : 0)
			.arg(item->canDelete() ? 1 : 0)
			.arg(item->allowsForward() ? 1 : 0)
			.arg(item->canBeSelected() ? 1 : 0)
			.arg(item->out() ? 1 : 0)
			.arg(item->mainView() ? 1 : 0)
			.arg(item->history()->maybeMessages() ? 1 : 0)
			.arg(item->history()->lastServerMessage() == item ? 1 : 0));
}

HistoryItem *InjectServiceMessage(
		not_null<History*> history,
		const MTPMessageAction &action,
		ServiceMessageFields fields,
		rpl::lifetime &lifetime) {
	using Flag = MTPDmessageService::Flag;
	const auto owner = &history->owner();
	const auto from = fields.from
		? not_null<PeerData*>(fields.from)
		: history->peer;
	const auto id = fields.local
		? owner->nextLocalMessageId()
		: owner->nextNonHistoryEntryId();
	const auto actionType = action.type();
	auto refusal = QString();
	if (!history->folderKnown()) {
		refusal = u"folder unknown, newItemAdded requests a dialog entry"_q;
	} else if (!fields.local) {
		// The local shape is not isRegular() and reaches neither index gate.
		if (history->maybeMessages()) {
			refusal = u"messages index present, SparseIdsList::addNew "
				"asserts on an id above ServerMaxMsgId"_q;
		} else if (actionType == mtpc_messageActionChatEditPhoto
			|| actionType == mtpc_messageActionSuggestProfilePhoto) {
			refusal = u"chat photo action, refused by type: a real photo "
				"builds MediaPhoto and shared media indexing asserts on "
				"an id above ServerMaxMsgId"_q;
		}
	}
	if (!refusal.isEmpty()) {
		Fail(
			u"FIXTURE_UNSUPPORTED: tag=%1 id=%2 action=0x%3 local=%4"_q
				.arg(fields.diagnosticTag)
				.arg(qint64(id.bare))
				.arg(actionType, 0, 16)
				.arg(fields.local ? 1 : 0),
			refusal);
		return nullptr;
	}
	const auto localFlags = fields.local
		? MessageFlags(MessageFlag::Local)
		: MessageFlags();
	const auto serviceFlags = Flag::f_from_id | Flag::f_out;
	const auto item = history->addNewMessage(
		id,
		MTP_messageService(
			MTP_flags(serviceFlags),
			MTP_int(0), // id, replaced by the MsgId argument
			peerToMTP(from->id), // from_id
			peerToMTP(history->peer->id), // peer_id
			MTPPeer(), // saved_peer_id
			MTPMessageReplyHeader(), // reply_to
			MTP_int(base::unixtime::now()), // date
			action,
			MTPMessageReactions(), // reactions
			MTPint()), // ttl_period
		localFlags,
		NewMessageType::Unread);
	LogServiceMessagePredicates(item, fields.diagnosticTag);

	const auto fullId = item->fullId();
	const auto weak = base::make_weak(history);
	const auto tag = fields.diagnosticTag;
	lifetime.add([weak, fullId, tag] {
		const auto history = weak.get();
		const auto item = history
			? history->owner().message(fullId)
			: nullptr;
		LogRaw(u"FIXTURE_REMOVE: tag=%1 id=%2 historyAlive=%3 present=%4"_q
			.arg(tag)
			.arg(qint64(fullId.msg.bare))
			.arg(history ? 1 : 0)
			.arg(item ? 1 : 0));
		if (item) {
			item->destroy();
		}
	});
	return item;
}

} // namespace Test

#endif // _DEBUG
