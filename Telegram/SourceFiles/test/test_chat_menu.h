/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_msg_id.h"
#include "test/test_menu.h"

#include <QtCore/QPoint>
#include <QtCore/QPointer>
#include <QtCore/QRect>
#include <QtCore/QString>

class HistoryItem;
class QWidget;

namespace Test {

// A chat-list context menu, read in the turn that built it and attributed
// to the message the list resolved while it built it.
//
// Why it exists. In 2026/10/12/recognize-tgw-widget-documents (work/test.md,
// Test 25 in Runs 1-3 and the selected-state leg of Test 22 in Runs 1-3)
// four of seven Test::OpenContextMenu readings on HistoryInner silently
// belonged to other messages: built with atBuild={... appState=inactive
// activeWindow=none}, they read the action list of an earlier target, Run 1
// was first written as a PASS, and a "Select" triggered from one of them
// selected another message. Both chat lists resolve the message under a
// point in mouseActionUpdate(), which returns at once unless the list's
// _mouseActive is set or its window()->isActiveWindow() - and
// HistoryInner's also while hasPendingResizedItems(), for its history or
// the migrated one. showContextMenu() then builds the menu from whatever
// the last successful update left behind: Element::Moused(),
// Element::Hovered(), Element::HoveredLink() and _dragStateItem on
// HistoryInner, _overElement and _overItemExact on
// HistoryView::ListWidget. A wrong-message menu looks exactly like a valid
// one: same texts, built=1, no refusal.
//
// Delivery. The reader never activates a window: activation runs
// Window::MainWindow::handleActiveChanged, which can mark the shown
// messages read (Histories::readInboxTill aborts on a harness-injected
// regular item whose id is not a server id) and requests an online-status
// update. It opens the gate's other operand instead, for the length of the
// call:
//  1. a QEnterEvent to the list, inside Test::Settle, whose
//     enterEventHook sets _mouseActive (and resolves QCursor::pos(), the
//     real cursor - which is why attribution is never read here); the
//     state right after it drained is the afterEnter resolution;
//  2. Test::OpenContextMenu at |point| with its at-build observer, which
//     reads the list's actual resolution inside the delivery, after
//     showContextMenu's own mouseActionUpdate(e->globalPos()) and before
//     any postponed call drained;
//  3. always a QEvent::Leave to the list when it survives, inside
//     Test::Settle, whose leaveEventHook clears _mouseActive, the active
//     click handler and, on HistoryInner, Element::Hovered(); after it
//     _mouseActive is read again and must be false. Element::Moused() and
//     _overElement legitimately keep the last resolved view: they are not
//     hover, and nothing here claims to clear them.
// The leave also follows a delivery that ended in a refusal, and nothing
// is delivered at all for a request refused before the enter.
//
// Attribution. The reading is built() only when the resolution gate was
// open when the context-menu event was delivered - read right after the
// enter drained, because showContextMenu resolves first and only then
// builds and pops its menu - and the at-build observation shows that every
// candidate a menu entry can take its message from agrees with the
// requested message, not merely its view:
// - HistoryInner: Element::Moused() is the list's own view of the target,
//   Element::Hovered() and Element::HoveredLink() are that view or none,
//   and _dragStateItem is the target or none;
// - HistoryView::ListWidget: _overElement is the list's own view of the
//   target, and the item showContextMenu takes (_overItemExact, else
//   _overElement->data()) is the target.
// The fresh menu must also be the list's own _menu, the one
// showContextMenu builds (a reactions-manager or other component's menu is
// a foreign-menu refusal). The private state is read through the
// test-only explicit-instantiation accessor test_notify_override.cpp uses,
// confined to test_chat_menu.cpp: no production accessor, no pixel scan,
// no registry.
//
// Refusals, each named; the reason carries the values that decided it.
// Refused before anything is delivered:
// - no-list: |list| is null (a destroyed QPointer reads as one);
// - not-a-chat-list: |list| is neither a HistoryInner nor a
//   HistoryView::ListWidget, or a ListWidget with no window controller (the
//   chat preview's, whose showContextMenu builds nothing);
// - no-target: |target| is null;
// - foreign-target: the list shows no view of its own for the target
//   (HistoryInner: viewByItem with itemTop() >= 0; ListWidget:
//   viewByPosition answering an Element whose data() is the target and
//   whose delegate() is this list);
// - outside-viewport: |point| is outside the list's visible message
//   viewport, where mouseActionUpdate() would clamp it onto another row;
// - pending-resize: HistoryInner has pending resized items, for its
//   history or the migrated one, so its mouseActionUpdate() returns early.
//   HistoryWidget::paintEvent normally recounts that layout, so in a window
//   that gets no paints - minimized, as Test::KeepMainWindowNotMarkingRead
//   keeps it - it stays pending until the next
//   Data::Session::sendHistoryChangeNotifications(), and
//   History::addNewItem itself leaves one waiting. The reader never changes
//   the layout behind its caller's point: the caller processes it first,
//   outside any until and only while the window cannot mark read, with
//   history->owner().notifyHistoryChangeDelayed(history) for the pending
//   history and then sendHistoryChangeNotifications(), which
//   HistoryWidget::handleHistoryChange answers by recounting it, and reads
//   its points after that, as the self-test does;
// - ambiguous-target: the target belongs to a grouped message (album),
//   or the list already holds a selection (messages or text), so the menu
//   would be the group's or the selection's rather than the target's.
// Refused after the delivery, when the menu (if any) was built:
// - list-gone: the list was destroyed during the delivery;
// - target-gone: the target or its view was destroyed during the
//   delivery or while its postponed calls drained;
// - menu-refused: Test::OpenContextMenu refused (no-menu, several-menus),
//   or its one fresh menu was destroyed before the reading returned;
// - unresolved: the gate was closed when the event was delivered (no
//   mouse-active, no active window, or a pending resize), or at build the
//   list resolved no message;
// - wrong-target: at build the list resolved another message - the
//   resolved message is in the details;
// - ambiguous-target: at build the list resolved the target's view but a
//   candidate above names another message;
// - foreign-menu: the fresh menu is not the list's own _menu;
// - mouse-still-active: after the leave the list still reads
//   _mouseActive true.
// A refusal after the delivery hides the one menu this delivery built
// (Ui::PopupMenu::hideMenu(true), reported as |menuHidden|) and clears the
// reading's menu pointer, so TriggerContextMenuAction(reading.menu, ...)
// refuses menu-gone; the recorded identities, texts and resolutions stay.
// No refusal leaves a usable menu in the reading, and no menu that existed
// before the call is ever hidden.
//
// Lifetime. The list is held as a QPointer; the target is recorded by its
// FullMsgId before the enter and re-resolved through the list's session
// after every delivery and drain, before any item or view is dereferenced
// again. Every resolution is a value.
//
// Trigger. An entry of an accepted reading is triggered with
// TriggerContextMenuAction(reading.menu, text) in the same turn, while the
// menu still lives. On an accepted reading "Select" selects the target.
//
// Neither function Notes nor Fails; the caller judges the reading, as with
// Test::OpenContextMenu.
enum class ChatListMenuRefusal {
	None,
	NoList,
	NotAChatList,
	NoTarget,
	ForeignTarget,
	OutsideViewport,
	PendingResize,
	AmbiguousTarget,
	ListGone,
	TargetGone,
	MenuRefused,
	Unresolved,
	WrongTarget,
	ForeignMenu,
	MouseStillActive,
};

// One reading of what the list resolved, taken before the enter, right
// after the enter drained (the state the context-menu event is delivered
// into), at build (inside the delivery) and after the leave. Each is read
// in full, its view, candidates, gate and flags alike, so a premise can be
// logged from it. |view| is the message of the list's resolved view
// (HistoryInner: Element::Moused(); ListWidget: _overElement) and
// |viewIdentity| that view's address; |hovered| and
// |hoveredLink| are Element::Hovered() and Element::HoveredLink() on
// HistoryInner and stay empty on a ListWidget; |exact| is _dragStateItem
// on HistoryInner and _overItemExact on a ListWidget; |menuItem| is the
// message the product's showContextMenu takes for its item entries
// (HistoryInner's plain branch: Hovered, else HoveredLink; ListWidget:
// _overItemExact, else _overElement->data()). |targetShown| says the list
// still showed a view of its own for the requested message, |viewIsTarget|
// that the resolved view is exactly that view, and |viewOwned| that the
// resolved view is this list's own. |gateOpen| is the condition
// mouseActionUpdate() runs under. |read| stays false for a reading never
// taken.
struct ChatListResolution {
	FullMsgId view;
	FullMsgId hovered;
	FullMsgId hoveredLink;
	FullMsgId exact;
	FullMsgId menuItem;
	QString viewIdentity;
	bool read = false;
	bool targetShown = false;
	bool viewOwned = false;
	bool viewIsTarget = false;
	bool mouseActive = false;
	bool activeWindow = false;
	bool pendingResize = false;
	bool gateOpen = false;
};

// |menu| is the underlying Test::OpenContextMenu reading, with its menu
// pointer cleared on every refusal; |kind| is "HistoryInner" or
// "HistoryView::ListWidget"; |requested| and |requestedView| identify the
// target and the list's own view of it when the request was made;
// |viewport| is the list's visible message area in its coordinates.
// |enterSent| and |leaveSent| record the two pointer events, |menuRead|
// that Test::OpenContextMenu was called (|menu| is default otherwise and
// is printed as not read), |observed| that the at-build observer ran,
// |listMenu| that the fresh menu was the list's own _menu, |menuHidden|
// that a refusal hid it. |attributed| is set only by an observed, agreeing
// resolution; built() is never true on default state.
struct ChatListMenuReading {
	ContextMenuReading menu;
	ChatListResolution before;
	ChatListResolution afterEnter;
	ChatListResolution atBuild;
	ChatListResolution afterLeave;
	QPointer<QWidget> list;
	QString listIdentity;
	QString kind;
	QString requestedView;
	QString reason;
	FullMsgId requested;
	QPoint local;
	QRect viewport;
	ChatListMenuRefusal refusal = ChatListMenuRefusal::None;
	bool enterSent = false;
	bool leaveSent = false;
	bool menuRead = false;
	bool observed = false;
	bool listMenu = false;
	bool menuHidden = false;
	bool attributed = false;

	[[nodiscard]] bool built() const {
		return attributed && (refusal == ChatListMenuRefusal::None);
	}
};

// none, no-list, not-a-chat-list, no-target, foreign-target,
// outside-viewport, pending-resize, ambiguous-target, list-gone,
// target-gone, menu-refused, unresolved, wrong-target, foreign-menu,
// mouse-still-active.
[[nodiscard]] QString ChatListMenuRefusalName(ChatListMenuRefusal refusal);

// |list| is the HistoryInner or HistoryView::ListWidget that shows
// |target|, and |point| is in |list|'s coordinates, as with
// Test::OpenContextMenu; the caller already knows where its target is.
// The point is deliberately not required to lie on the target's view: a
// request whose point resolves another message is the wrong-target
// refusal, with that message in the details.
[[nodiscard]] ChatListMenuReading OpenChatListContextMenu(
	QWidget *list,
	HistoryItem *target,
	QPoint point);

// The one formatter for a reading, for a Check's details and a Note. It
// prints recorded values only: the requested and resolved messages and
// views, the four resolutions, the pointer events and the underlying
// ContextMenuDetails.
[[nodiscard]] QString ChatListMenuDetails(const ChatListMenuReading &reading);

// The reader measuring itself on both real chat lists, in twelve stages
// named "chat-list menu self-test: <stage>", over two harness-injected
// messages A and B in Saved Messages.
//
// Fixture. A and B are a private fixture of this module: two fixed,
// non-link plain-text outgoing regular messages, built from the current
// MTP_message argument list (GenerateForwardedItem in
// settings_privacy_controllers.cpp) and added with History::addNewMessage
// the way Test::InjectServiceMessage adds its service items - ids from
// Data::Session::nextNonHistoryEntryId(), above ServerMaxMsgId and outside
// the client range, no MessageFlag::Local - so they are selectable, which
// no service item is. Nothing is sent to the server, and the only menu
// entry ever triggered is "Select". They are injected only when the
// history's folder is known, it is loaded at its bottom, no
// History::messages() index exists yet and the first loaded regular message
// is a server message, and every predicate is logged in a FIXTURE_TEXT row.
// Their hazards are InjectServiceMessage's: an id that is no server id
// aborts Histories::readInboxTill, so the self-test keeps the main window
// minimized with Test::KeepMainWindowNotMarkingRead from before the
// injection until they are destroyed, re-asserts that lever before every
// activation it causes, and never activates or un-minimizes the window
// while they exist; and the HistoryView::ChatWidget leg seeds
// History::messages() from the blocks, A and B included, so nothing is
// injected again after that.
//
// Stages.
//  1. Capture the baseline - window state, Qt activation, the
//     use-new-chat-view option, the active chat and every list's
//     selection - then minimize the window and prove it is not marking
//     read (named fixture gates).
//  2. Open Saved Messages in the legacy HistoryWidget, in the background
//     and at its end, and resolve its own HistoryInner by RTTI and
//     ownership (fixture gate: loaded at its bottom, folder known, no
//     messages index, no selection).
//  3. Inject A and B, process the pending layout the injection leaves (the
//     pending-resize remedy above), and wait for both to be laid out with
//     their points inside the visible message viewport; a wait that ends
//     with the layout pending again processes it once more and reads the
//     points again before the gate judges (fixture gates). Stages 4 and 6
//     process it too before they read their points.
//  4. HistoryInner premise, protected reading and Select, in one turn:
//     the attributed reading on A makes A the last resolved message; the
//     mouse is outside the list and Test::ClearWindowActive makes its
//     window inactive; an unprotected Test::OpenContextMenu at B's point
//     builds a menu the list resolved to A or to none, never to B, read at
//     build through the observer; the same conditions are re-established;
//     Test::OpenChatListContextMenu for B is built and attributed to B;
//     and "Select" from it leaves exactly [B] in getSelectedItems().
//  5. Clear the test's own selection and wait for its selection-mode
//     animation to finish.
//  6. HistoryInner controls, in one turn: B requested at A's point is the
//     wrong-target refusal with A resolved in its details, its menu hidden,
//     and "Select" through it refuses menu-gone and changes no selection;
//     a null list, the list's parent widget and a point below the viewport
//     are refused as no-list, not-a-chat-list and outside-viewport with
//     nothing delivered; and with the window made active by
//     Test::ForceWindowActive - still minimized and not marking read - the
//     reading for B resolves B.
//  7. Turn the option on and open the same Saved Messages in a
//     HistoryView::ChatWidget; resolve its ListWidget by viewByPosition and
//     data()/delegate() identity; a missing view of A or B is a fixture-gate
//     FAIL, never a skipped list.
//  8-10. The same three stages on the ListWidget, with getSelectedIds().
// 11. Teardown: hide the self-test's menus, clear its selections, leave the
//     lists, destroy A and B first and prove neither resolves to a message
//     or a list view, restore the option, show the found chat (or none)
//     again, and only then restore the window state and the Qt activation.
// 12. Wait for the restored window state plus a repaint settle, then put
//     the found Qt activation back and compare it with the baseline.
//
// Activation. Every Test::ClearWindowActive and Test::ForceWindowActive is
// undone in the same turn on every path, back to the activation captured
// in stage 1. That restore-to-found rule is the task's and holds for this
// self-test only: after a default background macOS launch the found state
// usually has no focus window, and the restore then clears it again.
//
// Gates and cleanup. Every stage after a failed fixture gate is N/A
// through its skipReason, with the gate's text; the teardown and the settle
// still run. The same idempotent release is registered with
// Runner::onFinish before anything is changed, so a stage timeout or the
// watchdog still destroys A and B, closes the menus and puts the option,
// the chat, the window and the activation back; after a normal teardown it
// only reads again. The History::messages() index the ChatWidget leg seeds
// may stay allocated, and its FIXTURE_INDEX row reports whether it still
// lists A or B: an id above ServerMaxMsgId sits above every slice's
// range.till, where SparseIdsList::removeOne cannot reach it, and no
// message stands behind it once A and B are destroyed.
void AppendChatListMenuSelfTest(not_null<Runner*> runner);

} // namespace Test
