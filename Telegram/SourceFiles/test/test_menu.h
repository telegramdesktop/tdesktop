/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "test/test_runner.h"

#include <QtCore/QPointer>
#include <QtCore/QRect>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtGui/QContextMenuEvent>

#include <vector>

namespace Test {

// A Ui::PopupMenu cannot be waited on through the visibility of the
// Ui::Menu it wraps. PopupMenu::startShowAnimation() ends with
// hideChildren() (ui/widgets/popup_menu.cpp), and the only path that shows
// them again during a normal open is the Ui::PostponeCall the final
// paintEvent queues once the show animation has drawn its last frame.
// showAnimationCallback() merely calls update(), and the other two
// showChildren() calls sit inside prepareCache()'s grab-and-restore and the
// opacity path that re-shows a menu after a hide
// (opacityAnimationCallback()). So the inner menu is hidden for the whole
// show animation and comes back only from a side effect no -testagent run
// is guaranteed to reach. A readiness over menu()->isVisible() then waits
// until focusOutEvent -> hideMenu() -> hideEvent() -> deleteLater() takes
// the popup away underneath it, which is how runs 4 and 5 of
// 2026/08/28/complete-server-history-details-hash-and-paging ended.
//
// The opposite mistake costs a run just as surely: a one-shot grab taken in
// the turn that called popup() usually lands on a show-animation frame with
// almost nothing painted on it, which is run 2's blank p1e_menu_peer_nohash.
// Usually, not always: on Windows that turn can already paint a partly
// shown frame (docs/helpers/test_menu.md#the-opening-turn-grab-row says
// why), so a same-turn grab is no frame to rely on either way.
//
// So the readiness here carries content identity only - the widget is a
// Ui::PopupMenu, it is visible, its geometry is non-empty and it carries
// actions - and the shared PreparedWidgetCapture decides when the frame is
// good, because refusing a blank frame is exactly the check a menu capture
// needs and it already exists. showingContent is read and printed in every
// details line so a log reader sees it, and it is never part of the gate.
//
// Two more facts a caller gets wrong otherwise. Test::PaintingLayerRoot()
// must not be used on a popup: PopupMenu::init() sets
// Qt::WA_NoSystemBackground, so the harness's blank-root refusal never
// applies to it, and the popup is its own top-level window
// (Qt::FramelessWindowHint | Qt::BypassWindowManagerHint | Qt::Popup |
// Qt::NoDropShadowWindowHint), so that walk refuses on its first hop. And
// the hidden-children premise above is platform-dependent rather than
// universal: init() takes _useTransparency from
// Platform::TranslucentWindowsSupported() and startShowAnimation() returns
// before hideChildren() when it is false. useTransparency() is public for
// exactly that reason, every reading below reports it, and the self-test
// asserts it as a named fixture gate instead of passing vacuously on a host
// without translucent windows - on Windows it is an inline return true
// (ui/platform/win/ui_utility_win.h).
//
// This is its own module rather than part of test_capture.h because
// CapturePopupMenu appends Runner stages, and test_runner.cpp already
// includes test_capture.h: the reverse include would invert the harness's
// layering and make its most-included module runner-aware. The three pure
// readings below would fit test_capture.h's QWidget-only vocabulary
// unchanged; the stage appender is the whole of what decides this.
//
// The context-menu reader at the end of this header lives here too: it
// composes this module's ReadPopupMenu and PopupMenuReady with
// test_widgets.h's Settle, and its self-test with FindAll, and
// test_widgets.h does not include this header, so no cycle forms.
struct PopupMenuReading {
	bool isMenu = false;
	bool visible = false;
	bool transparent = false;
	bool showingContent = false;
	int width = 0;
	int height = 0;
	int actions = 0;
	QString identity;
};

// One reading, taken once, so a snapshot can be asserted stages after the
// turn it was taken in and so a pass and a refusal print the same fields.
[[nodiscard]] PopupMenuReading ReadPopupMenu(QWidget *widget);

// Content identity only, and never menu()->isVisible(): the popup exists,
// is visible, has non-empty geometry and carries actions.
[[nodiscard]] bool PopupMenuReady(QWidget *widget);

// The same reading as text, for a stage's timeoutDetails and for a Check's
// details. Names a null and a widget that is not a Ui::PopupMenu, and
// otherwise carries showingContent together with the reason it is reported
// and never required.
[[nodiscard]] QString PopupMenuDetails(QWidget *widget);

// Captures an open Ui::PopupMenu, or one that |open| opens. |open| runs in
// its own stage, and only when the resolver does not already answer a ready
// menu; the helper never calls popup() itself, because popup() on an empty
// menu hides and deleteLater()s it (popup_menu.cpp) and only the caller
// knows the position and the way the product opens its menu.
// |skipReason|, when given, becomes the Stage::skipReason of both stages
// this appends - the opener and the capture - read when each begins; left
// empty the helper behaves exactly as before. A locked Windows console is
// no reason to skip a popup-capture leg: arm Test::ArmPopupHold
// (test_popup_hold.h) before the menu opens and release it after the last
// leg that needs it open across turns, so the deactivation does not close
// the menu between turns.
void CapturePopupMenu(
	not_null<Runner*> runner,
	const QString &name,
	Fn<QWidget*()> resolve,
	Fn<void()> open = {},
	Fn<void(QWidget*, const QImage &)> inspect = {},
	crl::time timeout = kDefaultStageTimeout,
	Fn<QString()> skipReason = {});

// This helper measuring itself, in six stages. It opens a real
// Ui::PopupMenu of its own over the primary window and, in the turn that
// opened it, shows the two predicates disagreeing: the content-identity
// readiness accepts while the inner menu is still hidden. In that same turn
// it takes one PreparedWidgetCapture::prepare() of the menu, the
// opening-turn grab row. It then captures through both branches of
// CapturePopupMenu - the already-open one and, after closing the menu, the
// one that opens it - and asserts the refusal texts of a null, a widget
// that is no menu, an empty menu and a popup handed to the painting-layer
// root resolver.
//
// The opening-turn grab row, "a one-shot prepared grab in the opening turn
// refuses the frame instead of saving it". Its premise is the show
// animation's blank first frame, and it reads that premise from the menu
// itself: the open stage subscribes to Ui::PopupMenu::showStateValue() for
// its own call and keeps the last state emitted before prepare() and the
// last one emitted during it. Only the grab's own state decides; the
// earlier one is context. A grab that emitted appearing=1 at opacity 0
// reached the premise: PASS when prepare() refused it, FAIL when prepare()
// accepted it. Any other grab is a named "TEST_RESULT: N/A" whose reason
// begins "not reached:" and names an advanced frame painted, a show
// animation already finished or no state emitted by the grab, beside both
// states and the accepted flag - one launch's timing, never a pass and
// never a fixture-gate failure. The decision never reads LooksBlank, so a
// prepared capture that accepts the blank first frame still FAILs. The N/A
// is real on Windows, where the opening turn can paint a partly shown menu;
// docs/helpers/test_menu.md#the-opening-turn-grab-row says why.
//
// It needs no session, no chats list, no network and no account fixture.
// The only thing it asks of the process is a primary window to parent the
// menus to, and a missing one is reported as a named fixture gate instead
// of crashing. It appends its own teardown last, and it emits no deliberate
// failure: every refusal it demonstrates is observed through a value that
// logs nothing - PopupMenuReady returning false, PopupMenuDetails's text,
// PreparedWidgetCapture::prepare()'s pendingReason() - and is asserted as a
// passing Check whose details carry the refused reading verbatim. The one
// exception is the opening-turn grab row's named N/A above, written when
// that launch's grab did not paint the blank first frame. Its before-leg,
// the wedged stage this helper removes, is produced by gating on
// menu()->isVisible() and re-running the identical scenario, never by a
// stage that fails on purpose.
//
// A locked Windows console. The application is deactivated there and Qt
// closes a shown popup on QEvent::ApplicationDeactivate before the next
// turn: without a hold, Attempt 1 Runs 1 and 2 of
// 2026/10/07/add-a-same-turn-context-menu-reader-and-a-retried-clipboard-round-trip-to-the-test-harness
// (work/test.md) timed out on "target is not visible" right after the open
// stage passed. So the open stage arms Test::ArmPopupHold
// (test_popup_hold.h) with the label popup-menu-self-test right before it
// pops the menu, and Notes the Test::ReadConsoleLock() row
// (test_console_lock.h) it read there. The four stages that need the menu
// open across turns - the popup_menu_open capture, close, and the
// popup_menu_reopened opener and capture - then run and decide on a locked
// console as on an unlocked one. The teardown hides and destroys the
// fixture, releases the hold and Notes "popup menu self-test: popup hold
// released, deactivations stopped=<n> - <CONSOLE_LOCK row>"; when a
// timeout skips the teardown, Runner::onFinish releases it. No Check
// asserts a nonzero count: whether a deactivation reaches the open menu
// depends on the console. The self-test holds and releases its own arm, so
// a campaign appends it outside its own popup hold, never inside one. The
// translucency premise stays a named hard fixture gate Check: a host
// without translucent windows can never certify it. Off Windows the hold
// stops nothing: on macOS a packed launch with CGSSessionScreenIsLocked
// true ran this self-test with 0 FAIL
// (2026/10/06/add-a-post-paint-capture-sampler-to-the-harness,
// work/test.md, Test 2), because a locked macOS console does not close a
// shown popup.
void AppendPopupMenuCaptureSelfTest(not_null<Runner*> runner);

// A context menu, read in the turn that built it.
//
// Why it exists. Run 1 of
// 2026/10/07/show-url-tooltips-and-copy-link-on-rich-message-url-buttons
// (work/test.md, Tests 2-4) delivered a QContextMenuEvent, then polled on
// later event-loop turns for a visible, ready Ui::PopupMenu and read
// popup=0 for every target - the controls included, an ordinary keyboard
// URL button and a plain paragraph. Yet every product handler it drove
// builds its menu synchronously, inside the event, as a child of the
// widget that received it: HistoryInner::showContextMenu
// (base::make_unique_q<Ui::PopupMenu>(this, ...)),
// ListWidget::showContextMenu through FillContextMenu(this, ...) and
// MarkdownDocumentWidget::contextMenuEvent. The menu exists in the turn of
// the event; on that unattended console it was gone before the first poll,
// and the run did not diagnose why. Run 2 decided every leg by reading in
// the building turn instead (built=1 fresh=1, and ready=1 at build), and
// OpenContextMenu is that reading, shared.
//
// Delivery. The reader snapshots every live Ui::PopupMenu in the whole
// application (QApplication::allWidgets(), not only the receiver's
// subtree), so a handler that parents its menu to another widget is still
// read, and so is an ancestor that takes the propagated event -
// QApplication::notify re-sends an unaccepted ContextMenu up the parent
// chain until a window or Qt::WA_NoMousePropagation stops it. It then
// sends the QContextMenuEvent inside Test::Settle, takes the at-build
// reading inside the settle action right after sendEvent returns, before
// any postponed call runs, and the after-settle reading right after Settle
// drained them. No event-loop turn runs in between: no processEvents, no
// nested loop. One Windows caveat: PopupMenu::showPrepared calls
// ForceFullRepaintSync, which sends a synchronous QEvent::UpdateRequest,
// and Core::Sandbox::notify drains the crl::on_main queue on every
// UpdateRequest, so crl::on_main work may run inside the delivery. Qt
// posted events and timers cannot.
//
// What is read. "New" means not alive in the snapshot; the snapshot holds
// QPointers, so a menu destroyed and another allocated at its address
// never reads as the old one. A new menu whose parent is a Ui::PopupMenu
// is a submenu (PopupMenu::ensureSubmenu and addAction(text, submenu) both
// parent it to its menu) and is not counted. Exactly one new menu is the
// reading; none is the no-menu refusal, and more than one is the
// several-menus refusal, which names every candidate and takes none. A
// menu that exists after the event but is hidden or empty is reported,
// not refused - its atBuild says so; the products destroy an empty menu
// synchronously (_menu = nullptr), which correctly reads no-menu. A
// handler that re-pops a menu it kept from an earlier event builds nothing
// new, so it reads no-menu with reshown above zero. The texts are every
// non-separator entry in order, an entry whose QAction has no text kept in
// place as an empty string so positions match, and |actions| counts the
// separators too. A trailing separator is already gone from a popped menu:
// popup() removes it (prepareGeometryFor -> Menu::clearLastSeparator).
//
// Lifetime. The receiver, the menu and the menu's parent are held as
// QPointer<QWidget>, and everything else is a value, so a reading stays
// safe to keep, copy and print after its menu dies: the pointers read null
// and the identities, texts, counts and lifecycles stay as recorded. Print
// the recorded identity, never WidgetDescription of a pointer that may be
// gone.
//
// Reason. Mouse by default; Keyboard passes through, and what it means is
// the product's business - MarkdownDocumentWidget positions a keyboard
// menu at QCursor::pos(), for instance. The helper does not hover: a
// caller whose handler reads the hovered link hovers first. A chat list -
// HistoryInner or HistoryView::ListWidget - resolves the message under the
// point only while its window is active or the mouse is inside it, so a
// menu read there with this helper alone may belong to another message:
// read those with Test::OpenChatListContextMenu (test_chat_menu.h).
//
// At-build observer. |observeAtBuild|, when given, is called once,
// synchronously, inside the settle action: after the event was delivered
// and the fresh menu was collected, immediately before the at-build
// lifecycle is read, and so before any Ui::PostponeCall the delivery
// queued has drained. It is called only while the receiver is alive - not
// for a null receiver and not for one its own handler destroyed - and it
// may run when no fresh menu exists (a no-menu or several-menus reading).
// It is for observation only: it must not deliver events, mutate product
// state, pop or hide menus, or turn the event loop, because the reading
// it sits inside is taken in that same call. Left empty, the reader
// behaves exactly as before. test_chat_menu.cpp reads the list's resolved
// message through it.
//
// No pixels. A grab in the building turn usually lands on a blank
// show-animation frame, and on Windows sometimes on a partly shown one (the
// comment at the top of this file), so the reader takes none.
// A frame stays with CapturePopupMenu, as a supplementary leg taken while
// the menu is still open.
//
// Positive control. A no-menu reading counts as a product result only
// beside a positive control that the same helper built in the same
// widget; on its own it says nothing about the product.
//
// Trigger. Every Ui::Menu entry a user activates ends in QAction::trigger()
// (PopupMenu::handleTriggered, ui/widgets/popup_menu.cpp), and
// Ui::Menu::CreateAction connects QAction::triggered with
// Qt::QueuedConnection and the action itself as the context
// (ui/widgets/menu/menu_common.cpp), so an entry's callback is a
// QMetaCallEvent posted to its action. TriggerContextMenuAction finds the
// one non-separator entry whose text equals |text| exactly on the
// still-live menu and, inside Test::Settle, calls trigger() and then
// QCoreApplication::sendPostedEvents(action, QEvent::MetaCall), which
// delivers exactly the calls posted to that action and leaves every other
// receiver's queued call pending. The callback has therefore run when it
// returns, before any event-loop turn. It does not hide the menu - the
// click path's hideMenu() is handleTriggered's own - and |after| says
// whether the menu is still open. Work the callback itself queues through
// crl::on_main, InvokeQueued or a timer still needs a bounded wait.
//
// Trigger refusals, each with nothing triggered: menu-gone (the reading
// holds no live menu), no-such-action (its reason lists the texts the
// menu holds), several-actions (an ambiguous text is refused, never
// resolved by taking the first match), action-disabled (a user cannot
// trigger a disabled entry, and Qt 5's QAction::activate emits triggered
// for one while Qt 6's ignores it, so the helper refuses it on both) and
// submenu-entry (a user's click opens the submenu instead of running the
// entry's callback). The last is decided by action->menu(), which covers
// both lib_ui routes that give an entry a submenu through a QMenu:
// MenuCallback's .fillSubmenu, which sets a dummy child QMenu, and
// PopupMenu::addAction(text, submenu). An entry given a submenu by a bare
// ensureSubmenu with no QMenu (ui/controls/who_reacted_context_action.cpp,
// for example) cannot be told apart through public API and is triggered
// like an ordinary entry.
//
// Neither function Notes nor Fails; the caller judges the reading, as
// with DropText.
enum class ContextMenuRefusal {
	None,
	ReceiverGone,
	NoMenu,
	SeveralMenus,
};

// One lifecycle reading of the menu a ContextMenuReading holds - at build,
// after the postponed calls settled, after a trigger or on a caller's own
// later poll - so every one of them prints alike. |activePopup| is "self",
// "other" or "none" (QApplication::activePopupWidget()); |appState| is
// "active", "inactive", "hidden" or "suspended"; |activeWindow| is
// QApplication::activeWindow()'s identity or "none"; |focusWindow| is
// "menu", "receiver-window", "other" or "none"
// (QGuiApplication::focusWindow() against the menu's own window handle and
// the receiver's window's). The application fields are read even when the
// menu is gone, and the menu fields then stay false and empty.
struct ContextMenuLifecycle {
	QRect geometry;
	QString activePopup;
	QString appState;
	QString activeWindow;
	QString focusWindow;
	int actions = 0;
	bool alive = false;
	bool visible = false;
	bool hidden = false;
	bool ready = false;
};

// One Ui::PopupMenu alive right before the event, as the reader found it,
// held as a QPointer so a menu destroyed since matches nothing.
struct ContextMenuSnapshotEntry {
	QPointer<QWidget> menu;
	bool visible = false;
};

// |receiver|, |identity| and |parentIdentity| are WidgetDescription values
// recorded while their widgets lived ("none" is printed for an empty one);
// |freshMenus| names every new candidate, so a several-menus refusal shows
// them all; |reason| is the refusal's text and stays empty when built().
// |before| is the pre-event snapshot every take compares against, and
// |retakes| counts the RetakeContextMenu calls that repeated the take.
struct ContextMenuReading {
	QPointer<QWidget> receiverWidget;
	QPointer<QWidget> menu;
	QPointer<QWidget> parent;
	std::vector<ContextMenuSnapshotEntry> before;
	QString receiver;
	QString identity;
	QString parentIdentity;
	QStringList freshMenus;
	QStringList texts;
	ContextMenuLifecycle atBuild;
	ContextMenuLifecycle afterSettle;
	QString reason;
	QPoint local;
	QPoint global;
	QContextMenuEvent::Reason eventReason = QContextMenuEvent::Mouse;
	ContextMenuRefusal refusal = ContextMenuRefusal::None;
	int menusBefore = 0;
	int menusAfter = 0;
	int fresh = 0;
	int reshown = 0;
	int actions = 0;
	int separators = 0;
	int retakes = 0;
	bool receiverEnabled = false;
	bool receiverVisible = false;
	bool delivered = false;
	bool accepted = false;
	bool receiverAlive = false;
	bool parentIsReceiver = false;

	[[nodiscard]] bool built() const {
		return (refusal == ContextMenuRefusal::None);
	}
};

[[nodiscard]] QString ContextMenuRefusalName(ContextMenuRefusal refusal);

// |point| is in |receiver|'s coordinates. A null |receiver| - a destroyed
// QPointer reads as one - is the receiver-gone refusal and nothing is
// sent; a receiver destroyed by its own handler is the same refusal, with
// the counts it reached kept. |observeAtBuild| is the optional at-build
// observer described above; neither refusal calls it.
[[nodiscard]] ContextMenuReading OpenContextMenu(
	QWidget *receiver,
	QPoint point,
	QContextMenuEvent::Reason reason = QContextMenuEvent::Mouse,
	Fn<void()> observeAtBuild = {});

// The same take on a later turn, for a receiver that accepts the event and
// builds its menu only once an answer arrives: against the reading's own
// |before| snapshot, with the event's rules (one new menu, submenus not
// counted, a kept menu shown again only counted in |reshown|). It repeats
// the take only while the reading is no-menu and its receiver lives, and
// otherwise changes nothing. A repeat counts in |retakes|, recounts the
// menus, texts, actions and separators, records the menu and its parent
// and reads |afterSettle| again; |atBuild| stays the event's own turn's. It
// sends nothing, calls no observer, hides nothing and turns no event loop.
// Answers built(). A menu built and closed between two calls is never seen
// (docs/helpers/test_menu.md#menus-built-on-a-later-turn).
[[nodiscard]] bool RetakeContextMenu(ContextMenuReading &reading);

// Pure: no grab, no mutation, safe on a reading whose menu is gone. A
// caller polls it on later turns to watch the menu close, if it must.
[[nodiscard]] ContextMenuLifecycle ReadContextMenuLifecycle(
	const ContextMenuReading &reading);
[[nodiscard]] QString ContextMenuLifecycleText(
	const ContextMenuLifecycle &lifecycle);

// The one formatter for a reading, for a Check's details and a Note. It
// prints recorded values only.
[[nodiscard]] QString ContextMenuDetails(const ContextMenuReading &reading);

enum class MenuTriggerRefusal {
	None,
	MenuGone,
	NoSuchAction,
	SeveralActions,
	ActionDisabled,
	SubmenuEntry,
};

// |matches| is recorded on every path that looked at the menu; |after| is
// read on every path before the helper returns.
struct MenuTrigger {
	QString text;
	ContextMenuLifecycle after;
	QString reason;
	MenuTriggerRefusal refusal = MenuTriggerRefusal::None;
	int matches = 0;
	bool triggered = false;
	bool actionAlive = false;
};

[[nodiscard]] QString MenuTriggerRefusalName(MenuTriggerRefusal refusal);
[[nodiscard]] MenuTrigger TriggerContextMenuAction(
	const ContextMenuReading &reading,
	const QString &text);
[[nodiscard]] QString MenuTriggerDetails(const MenuTrigger &trigger);

// A custom row, read by its exact full accessible name and activated by real
// keys (docs/helpers/test_menu.md#accessible-custom-rows).
//
// ReadMenuItem walks |popup|'s current Ui::Menu: every non-separator action
// resolved to its row by Menu::itemForAction and compared by the row's whole
// accessibilityName() - never by index, prefix or QAction::text(), which a
// custom row such as AboutAction (ui/boxes/peer_qr_box.cpp) leaves empty.
// |names| lists every row's name. Exactly one enabled match is found(), and
// only then are the row and its QAction held, beside the popup and the Menu.
// Refusals: menu-gone (no live Ui::PopupMenu), no-such-item (also an empty
// name), several-items and item-disabled.
//
// ActivateMenuItem sends nothing for a refused reading. Otherwise it checks
// the binding before any key and after every Down - the popup alive, its
// current Menu the captured one, the row and its QAction alive and still
// mapped to each other there under the same name, still unique and enabled,
// the popup ready - refusing menu-gone, menu-replaced, item-gone, item-stale,
// several-items or item-disabled; a same-named replacement or a live row of
// a replaced Menu is never taken for the captured one. It sends Down through
// Test::PressKey until the captured row is the Menu's Keyboard-selected row,
// at most |bound| (the Menu's actions, separators included) times, else
// not-selected, then exactly one Return. Nothing is read after the Return:
// |returnSent| is not proof that a callback ran. It never triggers a QAction,
// calls a callback, activates a window or turns the event loop. Neither
// function Notes or Fails, and a reading owns nothing and prints safely after
// its objects die.
enum class MenuItemRefusal {
	None,
	MenuGone,
	MenuReplaced,
	NoSuchItem,
	SeveralItems,
	ItemDisabled,
	ItemGone,
	ItemStale,
	NotSelected,
};

struct MenuItemReading {
	QPointer<QWidget> popup;
	QPointer<QWidget> menu;
	QPointer<QWidget> item;
	QPointer<QObject> action;
	QString name;
	QStringList names;
	QString reason;
	MenuItemRefusal refusal = MenuItemRefusal::None;
	int matches = 0;

	[[nodiscard]] bool found() const {
		return (refusal == MenuItemRefusal::None);
	}
};

// activated() means the Return was sent with no refusal before it.
struct MenuItemActivation {
	QString name;
	QString reason;
	MenuItemRefusal refusal = MenuItemRefusal::None;
	int steps = 0;
	int bound = 0;
	bool returnSent = false;

	[[nodiscard]] bool activated() const {
		return returnSent && (refusal == MenuItemRefusal::None);
	}
};

[[nodiscard]] QString MenuItemRefusalName(MenuItemRefusal refusal);
[[nodiscard]] MenuItemReading ReadMenuItem(
	QWidget *popup,
	const QString &accessibleName);
[[nodiscard]] MenuItemActivation ActivateMenuItem(
	const MenuItemReading &reading);
[[nodiscard]] QString MenuItemReadingDetails(const MenuItemReading &reading);
[[nodiscard]] QString MenuItemActivationDetails(
	const MenuItemActivation &activation);

// The reader measuring itself, in eight stages, over hidden harness widgets
// parented to the primary window whose handlers build their Ui::PopupMenus
// the way the products do - so every popup is its own window with a live
// parent, and none is ever popped without one.
//  1. A handler that builds and pops a menu in the receiving widget:
//     exactly one new menu, its texts in order without the separator, the
//     receiver as its parent, and at build a shown, ready popup that is
//     the application's active popup.
//  2. The same handler for a Keyboard-reason event: the reason reaches the
//     handler, and the first reading, whose menu that handler destroyed,
//     still prints its recorded identity and texts.
//  3. A handler that parents its menu to another widget: the
//     application-wide snapshot finds it, while a search rooted at the
//     receiving widget finds nothing.
//  4. A handler that builds and discards an empty menu: no-menu, beside
//     the positive controls of stages 1 and 2 in the same widget.
//  5. Refusals: several-menus for a handler that builds two, receiver-gone
//     for a null receiver, a submenu built the product way not counted as
//     a second menu, and several-actions and submenu-entry from the
//     trigger.
//  6. Trigger: First's Ui::Menu::CreateAction callback has run when
//     TriggerContextMenuAction returns while a marker queued just before
//     it has not, the marker runs on a later turn, and a raw trigger() of
//     a standalone CreateAction action stays queued until then and is not
//     delivered by the helper (the premise); then no-such-action,
//     action-disabled and, once the fixture dropped the menu, menu-gone.
//  7. The at-build observer: for a handler that builds its menu and queues
//     a Ui::PostponeCall marker, the observer runs exactly once, after the
//     handler ran and before the marker drained, and the marker has run
//     when the reader returns; for a receiver destroyed before the call
//     (its QPointer reads null) the reader refuses receiver-gone and the
//     observer is not called.
//  8. Teardown.
//
// It needs no session, no chats list, no network and no account fixture.
// The only thing it asks of the process is a primary window to parent its
// widgets to: a missing one fails stage 1's named fixture-gate Check, and
// every later stage but the teardown is N/A by that gate. It releases its
// fixture from the last stage and again from Runner::onFinish (idempotent),
// so a timed-out stage leaves no popup or widget behind. It emits no
// deliberate failure: every refusal it demonstrates is a returned value,
// asserted as a passing Check whose details print the reading.
void AppendContextMenuReaderSelfTest(not_null<Runner*> runner);

// The accessible-row route measuring itself, in five stages, over popups of
// its own parented to the primary window: lookup (an empty-text custom row
// bound by its full name beside an ordinary sibling, while a missing name,
// the label alone, a duplicate and a disabled row refuse with no key and the
// old trigger refuses the label), activation (the row's callback runs exactly
// once), lifetime before input and across a Down's settlement (destroyed
// rows, destroyed popups and stashed Menus against same-named replacements),
// and teardown. It needs no session or account; a missing primary window
// fails the first stage's fixture-gate Check and makes every later stage but
// the teardown N/A, and Runner::onFinish repeats the teardown's release.
void AppendMenuItemActivationSelfTest(not_null<Runner*> runner);

} // namespace Test
