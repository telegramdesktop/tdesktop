/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_transfer_messages.h"

#include "base/unixtime.h"
#include "data/components/top_peers.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "main/main_session.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_session.h"

namespace Wallet {
namespace {

[[nodiscard]] bool SameTransfer(
		not_null<HistoryItem*> item,
		const MTPDmessageActionGramTransfer &action,
		const QString &servedAddress) {
	const auto transfer = item->Get<HistoryServiceGramTransfer>();
	if (!transfer
		|| transfer->amount != action.vamount().v
		|| transfer->commentEncrypted != action.is_comment_encrypted()
		|| CanonicalAddress(transfer->peerAddress) != servedAddress) {
		return false;
	}
	return transfer->commentEncrypted
		|| (transfer->comment == qs(action.vcomment().value_or_empty()));
}

} // namespace

TransferMessages::TransferMessages(not_null<Main::Session*> session)
: _session(session) {
	_session->data().itemIdChanged(
	) | rpl::on_next([=](Data::Session::IdChange change) {
		for (auto &entry : _entries) {
			if (entry.id.peer == change.newId.peer
				&& entry.id.msg == change.oldId) {
				entry.id = change.newId;
				break;
			}
		}
	}, _lifetime);
}

FullMsgId TransferMessages::create(const SendArgs &args, uint64 randomId) {
	const auto user = args.userId
		? _session->data().userLoaded(args.userId)
		: nullptr;
	if (!user) {
		return {};
	}
	const auto history = _session->data().history(user);
	const auto localId = _session->data().nextLocalMessageId();
	const auto address = FormatFriendly(args.destination, args.bounce);
	const auto encrypted = !args.comment.text.isEmpty()
		&& !args.comment.isPublic;
	const auto publicText = args.comment.isPublic
		? args.comment.text
		: QString();
	using Flag = MTPDmessageService::Flag;
	using ActionFlag = MTPDmessageActionGramTransfer::Flag;
	auto actionFlags = MTPDmessageActionGramTransfer::Flags();
	if (!publicText.isEmpty()) {
		actionFlags |= ActionFlag::f_comment;
	}
	if (encrypted) {
		actionFlags |= ActionFlag::f_comment_encrypted;
	}
	const auto message = MTP_messageService(
		MTP_flags(Flag::f_out | Flag::f_from_id),
		MTP_int(0), // id, replaced by the MsgId argument
		peerToMTP(_session->userPeerId()), // from_id
		peerToMTP(user->id), // peer_id
		MTPPeer(), // saved_peer_id
		MTPMessageReplyHeader(), // reply_to
		MTP_int(base::unixtime::now()), // date
		MTP_messageActionGramTransfer(
			MTP_flags(actionFlags),
			MTP_long(args.amountNano),
			MTP_string(address),
			MTP_string(QString()), // transaction_id, only the server assigns it
			publicText.isEmpty() ? MTPstring() : MTP_string(publicText)),
		MTPMessageReactions(), // reactions
		MTPint()); // ttl_period
	const auto item = history->makeMessage(
		localId,
		message.c_messageService(),
		MessageFlag::Local | MessageFlag::BeingSent);
	history->addNewLocalMessage(item);
	_session->data().registerMessageRandomId(randomId, item->fullId());
	_entries.push_back({ item->fullId(), randomId });
	return item->fullId();
}

void TransferMessages::dropSending(FullMsgId id) {
	const auto i = ranges::find(_entries, id, &Entry::id);
	if (i == end(_entries)) {
		return;
	}
	const auto randomId = i->randomId;
	_entries.erase(i);
	_session->data().unregisterMessageRandomId(randomId);
	if (const auto item = _session->data().message(id)) {
		if (item->isSending() && IsClientMsgId(item->id)) {
			item->destroy();
		}
	}
}

HistoryItem *TransferMessages::adopt(
		not_null<History*> history,
		MsgId id,
		const MTPDmessageService &data) {
	if (_entries.empty()
		|| data.vaction().type() != mtpc_messageActionGramTransfer
		|| !data.is_out()) {
		return nullptr;
	}
	const auto &action = data.vaction().c_messageActionGramTransfer();
	const auto peerId = history->peer->id;
	const auto served = FullMsgId(peerId, id);
	auto servedAddress = std::optional<QString>();
	auto i = end(_entries);
	while (i != begin(_entries)) {
		--i;
		const auto item = _session->data().message(i->id);
		if (!item) {
			i = _entries.erase(i);
			continue;
		}
		const auto exact = (i->id == served);
		auto matched = exact;
		if (!matched
			&& i->id.peer == peerId
			&& item->isSending()
			&& IsClientMsgId(item->id)) {
			if (!servedAddress) {
				servedAddress = CanonicalAddress(qs(action.vpeer_address()));
			}
			matched = SameTransfer(item, action, *servedAddress);
		}
		if (!matched) {
			continue;
		} else if (!exact && _session->data().message(peerId, id)) {
			dropSending(i->id);
			return nullptr;
		}
		const auto randomId = i->randomId;
		_entries.erase(i);
		_session->data().unregisterMessageRandomId(randomId);
		if (IsClientMsgId(item->id)) {
			item->setRealId(id);
			_session->topPeers().increment(history->peer, item->date());
		}
		item->applyEdition(data);
		item->addToMessagesIndex();
		return item;
	}
	return nullptr;
}

} // namespace Wallet
