/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_fake_items.h"

#include "base/unixtime.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"

namespace HistoryView {

AdminLog::OwnedItem GenerateItem(
		not_null<HistoryView::ElementDelegate*> delegate,
		not_null<History*> history,
		PeerId from,
		FullMsgId replyTo,
		const QString &text,
		EffectId effectId) {
	Expects(history->peer->isUser());

	const auto item = history->addNewLocalMessage({
		.id = history->nextNonHistoryEntryId(),
		.flags = (MessageFlag::FakeHistoryItem
			| MessageFlag::HasFromId
			| MessageFlag::HasReplyInfo),
		.from = from,
		.replyTo = FullReplyTo{.messageId = replyTo },
		.date = base::unixtime::now(),
		.effectId = effectId,
	}, TextWithEntities{ .text = text }, MTP_messageMediaEmpty());

	return AdminLog::OwnedItem(delegate, item);
}

PeerId GenerateUser(not_null<History*> history, const QString &name) {
	Expects(history->peer->isUser());

	const auto peerId = Data::FakePeerIdForJustName(name);
	history->owner().processUser(MTP_user(
		MTP_flags(MTPDuser::Flag::f_first_name | MTPDuser::Flag::f_min),
		peerToBareMTPInt(peerId),
		MTP_long(0),
		MTP_string(name),
		MTPstring(), // last name
		MTPstring(), // username
		MTPstring(), // phone
		MTPUserProfilePhoto(), // profile photo
		MTPUserStatus(), // status
		MTP_int(0), // bot info version
		MTPVector<MTPRestrictionReason>(), // restrictions
		MTPstring(), // bot placeholder
		MTPstring(), // lang code
		MTPEmojiStatus(),
		MTPVector<MTPUsername>(),
		MTPint(), // stories_max_id
		MTPPeerColor(), // color
		MTPPeerColor())); // profile_color
	return peerId;
}

} // namespace HistoryView
