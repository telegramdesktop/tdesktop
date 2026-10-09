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
// the turn that called popup() lands on a show-animation frame with almost
// nothing painted on it, which is run 2's blank p1e_menu_peer_nohash.
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
// empty the helper behaves exactly as before. A campaign gates a
// popup-capture leg on a locked Windows console with
// [] { return Test::ConsoleLockGate(Test::ReadConsoleLock()); }
// (test_console_lock.h).
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
// readiness accepts while the inner menu is still hidden and a one-shot
// prepared grab still refuses. It then captures through both branches of
// CapturePopupMenu - the already-open one and, after closing the menu, the
// one that opens it - and asserts the refusal texts of a null, a widget
// that is no menu, an empty menu and a popup handed to the painting-layer
// root resolver.
//
// It needs no session, no chats list, no network and no account fixture.
// The only thing it asks of the process is a primary window to parent the
// menus to, and a missing one is reported as a named fixture gate instead
// of crashing. It appends its own teardown last, and it emits no deliberate
// failure: every refusal it demonstrates is observed through a value that
// logs nothing - PopupMenuReady returning false, PopupMenuDetails's text,
// PreparedWidgetCapture::prepare()'s pendingReason() - and is asserted as a
// passing Check whose details carry the refused reading verbatim. Its
// before-leg, the wedged stage this helper removes, is produced by gating
// on menu()->isVisible() and re-running the identical scenario, never by a
// stage that fails on purpose.
//
// A locked Windows console. The application is deactivated there and Qt
// closes the shown menu before the next turn: Attempt 1 Runs 1 and 2 of
// 2026/10/07/add-a-same-turn-context-menu-reader-and-a-retried-clipboard-round-trip-to-the-test-harness
// (work/test.md) timed out on "target is not visible" right after the open
// stage passed. So the four stages that need the menu open across turns -
// the popup_menu_open capture, close, and the popup_menu_reopened opener
// and capture - are TEST_RESULT: N/A by a skipReason that names the lock
// and prints the Test::ReadConsoleLock() reading (test_console_lock.h). It
// is read when the open stage pops the menu and again when each of those
// stages begins, and kept once it reads locked: a menu popped or held while
// locked is already closed. The open stage (decided from the opening turn),
// refusal text and the teardown still run and decide, the scenario
// continues past the self-test, and the open stage Notes the CONSOLE_LOCK
// row it read. The translucency premise stays a named hard fixture gate:
// Check, while the lock is a skip: a locked console is a scheduling
// condition - rerun on an unlocked console, where the same host certifies
// the capture (Run 3) - and a host without translucent windows can never
// certify that premise at all. Nothing is gated off Windows: on macOS a
// packed launch with CGSSessionScreenIsLocked true ran this self-test with
// 0 FAIL (2026/10/06/add-a-post-paint-capture-sampler-to-the-harness,
// work/test.md, Test 2), so Test::ConsoleLockGate answers only in a
// Windows build: on macOS the probe reads the session's lock and the open
// stage prints it, and nothing is gated.
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
// caller whose handler reads the hovered link hovers first.
//
// No pixels. A grab in the building turn lands on a blank show-animation
// frame (the comment at the top of this file), so the reader takes none.
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

// |receiver|, |identity| and |parentIdentity| are WidgetDescription values
// recorded while their widgets lived ("none" is printed for an empty one);
// |freshMenus| names every new candidate, so a several-menus refusal shows
// them all; |reason| is the refusal's text and stays empty when built().
struct ContextMenuReading {
	QPointer<QWidget> receiverWidget;
	QPointer<QWidget> menu;
	QPointer<QWidget> parent;
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
// the counts it reached kept.
[[nodiscard]] ContextMenuReading OpenContextMenu(
	QWidget *receiver,
	QPoint point,
	QContextMenuEvent::Reason reason = QContextMenuEvent::Mouse);

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

// The reader measuring itself, in seven stages, over hidden harness widgets
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
//  7. Teardown.
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

} // namespace Test
