/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

class History;
class HistoryItem;
class PeerData;

namespace rpl {
class lifetime;
} // namespace rpl

namespace Test {

// Optional injection knobs. The item is always outgoing: an incoming one
// reaches History::newItemAdded's !out() arm, which either requests a dialog
// entry or runs setUnreadCount -> setInboxReadTill and pins _inboxReadBefore
// at the fixture's above-ServerMaxMsgId id. History::setInboxReadTill is
// accumulate_max only, so nothing under test/ can undo that; a scenario that
// needs an incoming service item must bring a way to restore the state.
// |local| reproduces the pre-fixture MessageFlag::Local + client-id shape, so
// a scenario can log a negative control against the same code path.
struct ServiceMessageFields {
	PeerData *from = nullptr;
	bool local = false;
	QString diagnosticTag = u"service"_q;
};

// Injects |action| into |history| as a real history entry and returns the
// item, or null after one logged FIXTURE_UNSUPPORTED FAIL when the shape is
// refused below, so the caller must null-check it. The item is isRegular():
// no MessageFlag::Local, no MessageFlag::FakeHistoryItem, and an id from
// nextNonHistoryEntryId() that IsClientMsgId rejects, so it is never
// registered as a client-side message, and IsServerMsgId rejects too, so the
// server can never delete or edit it.
// It is isService() for every action except messageActionPhoneCall and
// messageActionConferenceCall, which the MTPDmessageService constructor turns
// into Data::MediaCall items with no HistoryServiceData component.
// The injection issues no MTP request, except for actions whose
// applyServiceChanges or newItemAdded arm reaches the network:
// messageActionSetChatWallPaper, messageActionSetChatTheme with a
// plain-emoticon theme, messageActionPaidMessagesPrice on an active broadcast
// peer, and any unique-gift action owned or hosted by the signed-in account,
// which includes the demonstrated star-gift card.
// Both shapes are refused when History::folderKnown() is false, because
// History::newItemAdded would then issue a requestDialogEntry MTP request.
// For the regular shape only, the id sits above ServerMaxMsgId, where
// SparseIdsList::addNew asserts: an existing History::messages() index is
// refused, and both chat-photo actions are refused by action type, which is
// conservative, because a photoEmpty payload builds no media and would not
// have asserted, while messageActionChatDeletePhoto is accepted. The hole
// left there is a dialog-row chat preview opened for the same peer after the
// injection, which can still seed History::messages() from this item.
// The second hole is that the id is not a server id, while
// Histories::readInboxTill(history, tillId) asserts IsServerMsgId(tillId), as
// History::readInboxTillNeedsRequest asserts it again.
// Histories::readInboxTill(item) reaches that assert for any isRegular()
// item, so HistoryInner::checkActivation aborts whenever the injected card is
// the bottom-most fully visible view of a shown chat while
// HistoryWidget::markingMessagesRead() is true, and the two accessibility
// focus paths abort whenever accessibility focus lands on it. Because
// History::setLastMessage makes this item the history's _lastServerMessage,
// Histories::readInbox aborts the same way on a send or a forward in that
// chat and on the dialogs "Mark as read". The paint path's readTill and
// Histories::readInboxOnNewMessage are closed only by the item being
// unconditionally out(), so f_out is load-bearing beyond the _inboxReadBefore
// reason above.
// Destroying |lifetime| destroys the item and removes it from the history; a
// history torn down first, an item already gone, a second destroy and several
// injections into one lifetime are all handled. With fields.local it injects
// the non-regular shape instead.
[[nodiscard]] HistoryItem *InjectServiceMessage(
	not_null<History*> history,
	const MTPMessageAction &action,
	ServiceMessageFields fields,
	rpl::lifetime &lifetime);

// Writes one line with the item's id and each menu-gating predicate
// separately, so an empty context menu is diagnosable from the log alone.
// InjectServiceMessage calls it once per injection; a scenario can call it
// again after an interaction to log the same fields in the same format. A
// null |item|, which is what a refused injection returns, logs one line
// naming the null instead of asserting.
void LogServiceMessagePredicates(
	HistoryItem *item,
	const QString &tag);

} // namespace Test
