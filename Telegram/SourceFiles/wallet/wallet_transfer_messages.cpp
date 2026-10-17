/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_transfer_messages.h"

#include "base/unixtime.h"
#include "data/components/promo_suggestions.h"
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

	// The server adds the suggestion with the first transfer, pushing nothing.
	_session->data().newItemAdded(
	) | rpl::filter([=](not_null<HistoryItem*> item) {
		return item->Has<HistoryServiceGramTransfer>()
			&& !IsClientMsgId(item->id)
			&& !_session->promoSuggestions().current(
				Data::PromoSuggestions::SugWalletFirstIncomingTransfer());
	}) | rpl::on_next([=] {
		_session->promoSuggestions().invalidate();
	}, _lifetime);
}

FullMsgId TransferMessages::create(const SendArgs &args, uint64 randomId) {
	// The served ids this registry settled from its own drafts are remembered
	// only to tell a draft's own message from a twin's, so they are useful
	// exactly while a draft is still sending. They are dropped here, and not
	// when the last entry is forgotten, because both settle paths forget the
	// entry before recording its served id, so a clear inside forget() would
	// always leave that last id behind.
	if (_entries.empty()) {
		_settled.clear();
	}
	const auto user = args.userId
		? _session->data().userLoaded(args.userId)
		: nullptr;
	if (!user) {
		return {};
	}
	const auto history = _session->data().history(user);
	// The floor is the newest message id this chat is known to hold at the
	// moment the draft is created. A message the server creates for this
	// transfer is always newer than that, so the floor can only ever refuse
	// a message that is not this draft's. History::maxMsgId() answers 0 for
	// a chat opened from a profile with no loaded blocks, so the dialogs
	// entry's lastServerMessage() is folded in; when neither is known the
	// floor stays 0, which is inert, because every server id is positive
	// and every client id is negative.
	const auto lastServed = history->lastServerMessage();
	const auto floor = std::max(
		history->maxMsgId(),
		lastServed ? lastServed->id : MsgId(0));
	const auto localId = _session->data().nextLocalMessageId();
	const auto address = FormatFriendly(args.destination, args.bounce);
	const auto encrypted = !args.comment.text.isEmpty()
		&& !args.comment.isPublic;
	// The draft carries a private comment too, which the served message
	// never does: the payload it will carry instead is written by the
	// chain, so until then this text is the only thing the card can show,
	// and it is the user's own. It never leaves this device.
	const auto commentText = args.comment.text;
	using Flag = MTPDmessageService::Flag;
	using ActionFlag = MTPDmessageActionGramTransfer::Flag;
	auto actionFlags = MTPDmessageActionGramTransfer::Flags();
	if (!commentText.isEmpty()) {
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
			commentText.isEmpty() ? MTPstring() : MTP_string(commentText)),
		MTPMessageReactions(), // reactions
		MTPint()); // ttl_period
	const auto item = history->makeMessage(
		localId,
		message.c_messageService(),
		MessageFlag::Local | MessageFlag::BeingSent);
	history->addNewLocalMessage(item);
	_session->data().registerMessageRandomId(randomId, item->fullId());
	_entries.push_back({ item->fullId(), randomId, floor });
	return item->fullId();
}

std::vector<TransferMessages::Entry>::iterator TransferMessages::forget(
		std::vector<Entry>::iterator i) {
	_session->data().unregisterMessageRandomId(i->randomId);
	return _entries.erase(i);
}

void TransferMessages::settle(
		not_null<HistoryItem*> item,
		MsgId id,
		const MTPDmessageService &data) {
	_settled.emplace(item->history()->peer->id, id);
	if (IsClientMsgId(item->id)) {
		item->setRealId(id);
		_session->topPeers().increment(item->history()->peer, item->date());
	}
	item->applyEdition(data);
	item->addToMessagesIndex();
}

HistoryItem *TransferMessages::forgetSending(FullMsgId id) {
	const auto i = ranges::find(_entries, id, &Entry::id);
	if (i == end(_entries)) {
		return nullptr;
	}
	forget(i);
	const auto item = _session->data().message(id);
	return (item && item->isSending() && IsClientMsgId(item->id))
		? item
		: nullptr;
}

void TransferMessages::dropSending(FullMsgId id) {
	if (const auto item = forgetSending(id)) {
		item->destroy();
	}
}

void TransferMessages::failSending(
		FullMsgId id,
		const QString &reason) {
	if (const auto item = forgetSending(id)) {
		if (const auto transfer = item->Get<HistoryServiceGramTransfer>()) {
			transfer->failReason = reason;
		}
		item->sendFailed();
		auto &owner = _session->data();
		owner.requestItemViewRefresh(item);
		// WHY: this verdict arrives from an MTP fail or an engine
		// callback, not from applyUpdates, so nothing else flushes the
		// pending refresh and the chat would stop repainting.
		owner.sendHistoryChangeNotifications();
	}
}

bool TransferMessages::refusePairing(
		not_null<HistoryItem*> item,
		MsgId newId) {
	const auto i = ranges::find(_entries, item->fullId(), &Entry::id);
	if (i == end(_entries)) {
		return false;
	}
	// Nothing at the served id means the generic promotion is right, so the
	// pairing is let through. An item this registry settled there from one of
	// its own drafts means an identical draft took this one's message, and
	// this draft's own message is still coming, so it keeps sending and keeps
	// its random id registered until that message settles it by content. An
	// item put there by anything else is this draft's own message, arrived
	// through a path which never reaches adopt(), so the draft it duplicates
	// is dropped here. Both occupied cases answer with a refusal, because the
	// generic handler may never destroy the server's settled card.
	const auto served = FullMsgId(i->id.peer, newId);
	if (!_session->data().message(served)) {
		return false;
	} else if (!_settled.contains(served)) {
		dropSending(i->id);
	}
	return true;
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
	const auto peerId = history->peer->id;
	const auto served = FullMsgId(peerId, id);
	// The whole registry is scanned for an entry which a pairing already
	// promoted to the served id before any content match is tried, so where
	// an entry sits in the vector cannot decide the outcome. Dead entries
	// are pruned here together with their random id registration. A served
	// id the chat already holds is never a draft's message, so it neither
	// adopts nor drops one; that refusal is also what keeps changeMessageId's
	// Ensures(ok) satisfiable, because no setRealId below can then target an
	// id which is already taken.
	for (auto i = begin(_entries); i != end(_entries);) {
		const auto item = _session->data().message(i->id);
		if (!item) {
			i = forget(i);
		} else if (i->id != served) {
			++i;
		} else {
			forget(i);
			settle(item, id, data);
			return item;
		}
	}
	if (_session->data().message(served)) {
		return nullptr;
	}
	// Among several drafts which match by content the oldest one is adopted.
	// Identical drafts are interchangeable to the user, so which of them is
	// chosen is invisible and only the determinism matters. An empty served
	// canonical address is refused, because CanonicalAddress answers empty
	// for anything it cannot parse and would otherwise match a draft whose
	// own address failed to parse as well. The comparison is id <= floor,
	// not id < floor, because an id equal to one the chat already held is
	// that same message served again, which may not be loaded in memory for
	// the refusal above to see.
	const auto &action = data.vaction().c_messageActionGramTransfer();
	const auto servedAddress = CanonicalAddress(qs(action.vpeer_address()));
	if (servedAddress.isEmpty()) {
		return nullptr;
	}
	for (auto i = begin(_entries); i != end(_entries); ++i) {
		if (i->id.peer != peerId || id <= i->floor) {
			continue;
		}
		const auto item = _session->data().message(i->id);
		if (!item
			|| !item->isSending()
			|| !IsClientMsgId(item->id)
			|| !SameTransfer(item, action, servedAddress)) {
			continue;
		}
		forget(i);
		settle(item, id, data);
		return item;
	}
	return nullptr;
}

} // namespace Wallet
