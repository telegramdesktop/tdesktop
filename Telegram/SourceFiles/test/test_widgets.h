/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtWidgets/QWidget>

namespace Ui {
class InputField;
} // namespace Ui

namespace Test {

// Telegram's custom widgets do not declare Q_OBJECT, so
// QObject::findChildren<T*>() cannot filter by their type and returns every
// child blindly cast to T* — using such a result crashes. Enumerate QWidget
// descendants (QWidget is a real Q_OBJECT) and dynamic_cast each instead.
template <typename T>
[[nodiscard]] std::vector<T*> FindAll(not_null<QWidget*> root) {
	auto result = std::vector<T*>();
	for (const auto widget : root->findChildren<QWidget*>()) {
		if (const auto typed = dynamic_cast<T*>(widget)) {
			result.push_back(typed);
		}
	}
	return result;
}

template <typename T>
[[nodiscard]] T *FindFirst(not_null<QWidget*> root) {
	const auto all = FindAll<T>(root);
	return all.empty() ? nullptr : all.front();
}

template <typename T>
[[nodiscard]] std::vector<T*> FindVisible(not_null<QWidget*> root) {
	auto result = FindAll<T>(root);
	result.erase(
		ranges::remove_if(result, [](T *widget) {
			return !static_cast<QWidget*>(widget)->isVisible();
		}),
		end(result));
	return result;
}

[[nodiscard]] QWidget *FindByObjectName(
	not_null<QWidget*> root,
	const QString &name);

// In-situ overlay hooks can publish the exact live object created by the
// product instead of rediscovering layer-owned/custom widgets from a window
// tree. Every replacement advances |generation|; dead QObjects resolve as
// unavailable. Publish calls are runtime no-ops unless Test::Active().
struct LiveWidgetSnapshot {
	QWidget *widget = nullptr;
	int generation = 0;
};

struct LiveActionSnapshot {
	bool available = false;
	int generation = 0;
	int invocationCount = 0;
	bool repeatable = false;
};

void PublishLiveWidget(
	const QString &key,
	not_null<QWidget*> widget);
[[nodiscard]] LiveWidgetSnapshot ReadLiveWidget(const QString &key);

void PublishLiveAction(
	const QString &key,
	not_null<QObject*> context,
	Fn<void()> action,
	bool repeatable = false);
[[nodiscard]] LiveActionSnapshot ReadLiveAction(const QString &key);
[[nodiscard]] bool InvokeLiveAction(const QString &key);

// A button's own disabled state, read beside Qt's.
// Ui::AbstractButton::setDisabled sets StateFlag::Disabled, which only
// isDisabled() reads back, and it hides QWidget::setDisabled instead of
// overriding it: called through a QWidget* the Qt slot runs instead. So a
// button the product blocked still answers QWidget::isEnabled() true, and
// isVisible() && isEnabled() is no readiness test for it.
//
// |disabled| is isDisabled(), |enabled| is QWidget::isEnabled() and
// |visible| is QWidget::isVisible(), each read independently in one call;
// usable() needs a read button that is visible, enabled and not disabled.
// A null widget, or one that is not a Ui::AbstractButton, is refused by
// name in |refusal|: |disabled| is then not read (ButtonStateDetails prints
// it as none) and the reading is never usable(). The reading logs nothing
// and holds no pointer; |identity| is the WidgetDescription taken in that
// call. Reference: docs/helpers/test_widgets.md, "A button's own disabled
// state".
struct ButtonStateReading {
	bool disabled = false;
	bool enabled = false;
	bool visible = false;
	QString identity;
	QString refusal;

	[[nodiscard]] bool read() const {
		return refusal.isEmpty();
	}
	[[nodiscard]] bool usable() const {
		return read() && visible && enabled && !disabled;
	}
};

[[nodiscard]] ButtonStateReading ReadButtonState(QWidget *widget);

// button=<identity> ownDisabled=<0|1|none> isEnabled=<0|1> visible=<0|1>
// usable=<0|1>, then " - <refusal>" when the reading was refused.
[[nodiscard]] QString ButtonStateDetails(const ButtonStateReading &reading);

// Input delivery and settlement contract.
//
// The input helpers deliver their events synchronously with
// QApplication::sendEvent from the calling (runner-stage) context. During
// each dispatch postponed-call processing is deferred, so a product
// fix-up queued by the event can never run mid-signal-emission, where
// Qt's re-entrant document machinery would swallow its own change
// notifications. After each delivered event every pending
// Ui::PostponeCall (and any calls those queue) runs to empty in a
// top-level context — the state real user input reaches at its own
// event's unwind.
//
// Why the defer and the explicit drain: Core::Sandbox tags each
// Ui::PostponeCall with the loop-nesting level current at queue time and
// runs it only at the unwind of an event whose nesting level matches
// that tag, and only while it sits newest in the queue. Under the
// harness's synthetic nesting a fix-up either starves outright or runs
// at a matching internal unwind INSIDE the sent event's own signal
// emission — both unlike real top-level input.
//
// Guarantee: when a helper returns, postponed text fix-ups have run AND
// their own change handling has run, so both the widget's document and
// the field's cached text state (InputField::getLastText) show the
// product's rewritten text. The drain covers Ui::PostponeCall only:
// crl::on_main, InvokeQueued, base::Timer and network completions still
// need bounded waits. If a drained call destroys the target widget, the
// helper skips its remaining events and returns.
//
// Pointer model: the harness has no pointer, so Click and Drag end
// with a QEvent::Leave, which leaves the widget they were given
// pointerless. Give them the widget that accepts the press: an ignored
// mouse event walks up the parent chain in QApplication::notify, so a
// click aimed at a non-accepting child sets Over on the ancestor
// button that took the press while the leave reaches only the child,
// and that ancestor stays hovered. The leave is also inert while
// StateFlag::Down is still set, because leaveEventHook returns before
// setOver on that branch. Without the leave
// Ui::AbstractButton::mousePressEvent's
// checkIfOver() latches StateFlag::Over for the rest of the process,
// Ui::RoundButton::paintEvent keeps painting textBgOver, and a later
// Test::DeriveBand under the style's normal fill returns ok=0 with no
// rows — an absence reading for a widget plainly on screen. Qt does
// not re-deliver an unhandled Leave to ancestors the way it
// re-delivers an ignored mouse or key event, so the leave stays on the
// target — but on the target it is an ordinary event. It runs that
// widget's own leaveEventHook, its direct parent's
// enterFromChildEvent, every subscriber of the target's
// RpWidget::events() stream, and every installed event filter. Two
// that matter: Ui::Menu::ItemBase deselects a menu entry on Leave, so
// clicking one no longer leaves it highlighted; and Ui::Tooltip keeps
// an application-wide filter for the life of the process, so any
// synthetic click starts its hide-by-leave timer. A widget that was
// never hovered keeps its Over unchanged, because setOver returns
// early on an unchanged value. ClickWithPressOrigin is the one helper
// that lends a press a pointer's origin, and it still ends with the
// same leave.
//
// DropText is the one exception to "after each delivered event": it
// settles once after its drag-enter + drop pair, because a drain between
// the two could destroy a viewport QDragManager still points at (see its
// comment).
//
// Provenance: Settle holds a Test::HarnessInputScope (test_console_shield.h)
// around its action and closes it before the drain, so an armed console
// shield passes every event delivered there as harness input - each
// helper's sent events, the shortcut or input-method events they produce,
// and an overlay's own spontaneous QWindowSystemInterface delivery wrapped
// in Settle. Nothing changes while the shield is not armed. Reference:
// docs/helpers/test_widgets.md, "Input helpers".
//
// Use Test::Settle for programmatic mutations; SettlePostponedCalls
// remains the bare drain.

// Synthesizes a full mouse press + release on the widget, at its center by
// default, then a QEvent::Leave, so a completed click leaves no hover
// behind. Drives the same event path as a real click.
void Click(not_null<QWidget*> widget, std::optional<QPoint> point = {});

// Click whose press starts the pressed button's own ripple at |point|, as
// a real pointer press there would. Ui::RippleButton::onStateChanged
// starts its press ripple at prepareRippleStartPosition(), and every class
// computes that as mapFromGlobal(QCursor::pos()) on itself plus its own
// offset and containment guard. The harness has no pointer, so a plain
// Click starts the ripple where the owner's real mouse happens to be -
// usually far outside the button - or starts none. Run 2 of
// 2026/10/20/replace-sell-with-card-and-color-actions-on-the-wallet-gift-cover
// read changedPixels=[0,0] in its ~40 and ~120 ms press frames for exactly
// that reason; 2026/08/03/redesign-add-funds-and-buy-grams and
// 2026/08/10/correct-streamed-draft-stop-colour-and-lookups had worked
// around it by moving the real cursor with QCursor::setPos, which changes
// hover on the owner's desktop and is forbidden. Added by
// 2026/10/20/let-harness-presses-start-button-ripples-at-the-press-point;
// its self-test is Test::AppendPressOriginSelfTest (test_press_origin.h).
//
// What it substitutes: only the origin prepareRippleStartPosition() reads.
// The ripple, its mask, colour, timing and paint, and the class's own
// offset and guard all stay the button's, and nothing is recomputed per
// class: a Ui::IconButton pressed inside the button but outside its
// ripple area starts no ripple, as a pointer press there would.
//
// Mechanism, all inside one Test::Settle action: read QCursor::pos(); take
// the floating target->mapFromGlobal(QPointF(cursor)) and move the target
// alone (public QWidget::move) by that mapping minus |point|, each axis
// rounded, so that the target's own mapFromGlobal(QCursor::pos()) reads
// |point| - the floating mapping, because a displacement taken from the
// already rounded QPoint mapping lands one pixel off on a half-pixel
// high-DPI window; verify that read; deliver Click's own press event
// (local |point|, global the target's real mapToGlobal(point), read before
// the move); move the target back. Only then does Settle drain the
// postponed calls. A non-native child's own geometry() is part of
// QWidget's global mapping up to the first QWindow, so the move shifts the
// target's mapping by exactly the displacement, whatever a platform
// sources QCursor::pos() from. The release, at the restored position, and
// the closing QEvent::Leave are Click's, through the same settle. The real
// cursor is only read, never set.
//
// For the press only, an event filter installed on the target last (Qt
// runs it before every earlier object filter) withholds the target's own
// two QEvent::Move - the facility's move and its move back; a move the
// press handler makes passes through - so RpWidget::geometryValue() owners
// and the earlier object filters never see the transient move, and it
// counts every QEvent::Paint that reaches the target while it is
// displaced. Qt::WA_Moved and Qt::WA_PendingMoveEvent are restored
// afterwards. Qt::WA_NoMousePropagation is set for the press only, so a
// press the target does not accept stops at the target instead of
// reaching its parent with a position taken from the displaced frame.
//
// What can observe the displaced state:
// - the press handler itself, synchronously: the target's and its
//   descendants' pos(), geometry(), mapToGlobal() and mapFromGlobal() read
//   the displaced frame - that is the substitution - so a handler that
//   places a popup from the target's mapToGlobal() on press places it at
//   the real cursor; QWidget::update() calls made there are stored
//   widget-relative and painted at the restored position;
// - application-level event filters still see the two QEvent::Move (none
//   of tdesktop's application-wide filters acts on a child widget's Move),
//   and an object filter the press handler installs on the target during
//   the press runs before the facility's and sees the move back;
// - accessibility: two LocationChanged notifications while an assistive
//   client is connected (QAccessible::isActive()); clients query bounds
//   asynchronously, after the restore;
// - nothing else: no paint, UpdateRequest, timer, crl::on_main or
//   InvokeQueued call can run while the target is displaced, because no
//   event loop turns, and every Ui::PostponeCall the press queues runs in
//   Settle's drain after the restore. A handler that calls repaint() or
//   processEvents() is the only exception, and |paintsWhileDisplaced|
//   reports it.
//
// Hover caveat: like Click, it ends with the QEvent::Leave, and the leave
// cancels the press's hover highlight before it paints (Ui::IconButton's
// hover animation restarts 1->0 from its still-zero value,
// Ui::RoundButton's isOver() is false again before any paint), so press
// frames show the ripple without the hover highlight.
//
// Not for drags: Drag and Test::AppendQueuedStartDrag keep the
// cursor-derived origin. A drag-start path can reach a nested
// QDrag::exec() loop from product code, which must never run while a
// target is displaced.
//
// Refusals (PressOriginRefusalName); each of the first four comes before
// anything is moved or sent:
// - top-level-target: the target is a window; moving it would move a real
//   OS window;
// - native-target: the target, or a descendant inside its window (separate
//   windows parented below it are skipped, since they do not move with
//   it), has internalWinId() or Qt::WA_NativeWindow; moving it would move
//   a real platform window, and a native target maps through its own
//   QWindow. |reason| names the native widget;
// - disabled-target: QWidget::event drops a disabled widget's mouse events
//   and the press would propagate to an ancestor;
// - point-outside: |point| lies outside the target's rect(), where no
//   pointer press reaches this widget;
// - origin-unreachable: after the move the target's
//   mapFromGlobal(QCursor::pos()) still is not |point| - a transformed
//   ancestor, a cursor that moved between the two reads, or a |point|
//   coordinate of 0 met on an exact half pixel; the target is moved back
//   and nothing is sent.
// No refusal depends on where the cursor is: the displacement is plain
// arithmetic on whatever QCursor::pos() returns, even off every screen.
//
// The reading. The helper neither Notes nor Fails; the caller judges the
// returned reading, as with TextDrop, and PressOriginDetails prints all of
// it on one line:
// - |pressSent|: the press reached the target;
// - |delivered|: press, release and leave each reached a live target, as
//   in Click;
// - |displaced|: the target was moved (false when the cursor already
//   mapped to |point|);
// - |restored|: the target's position is back where it was, after the
//   press or after an origin-unreachable refusal (false after the four
//   early refusals, which move nothing, and when the press destroyed the
//   target);
// - |pressAccepted|: the target accepted the press;
// - |targetAlive|: the target is alive when the helper returns;
// - |cursorStable|: |cursorAfter| equals |cursorBefore|;
// - |geometryChangedByPress|: the press handler moved or resized the
//   target. A moved target keeps the handler's position, its Qt::WA_Moved
//   and Qt::WA_PendingMoveEvent included; a target that was only resized
//   keeps its new size and still gets its position back;
// - |moveEventsWithheld|: QEvent::Move withheld from the target (2 for a
//   visible displaced target, 0 for a hidden one or when not displaced);
// - |paintsWhileDisplaced|: QEvent::Paint that reached the target while
//   it was displaced (expected 0);
// - |point|: the press point in the target's coordinates (its centre by
//   default);
// - |cursorBefore|, |cursorAfter|: QCursor::pos() read before the move and
//   right after the press;
// - |displacement|: how far the target was moved;
// - |originSeen|: target->mapFromGlobal(QCursor::pos()) read after the
//   move, right before the press - the expression the classes evaluate;
// - |pressGlobal|: the press event's global position, the target's real
//   mapToGlobal(point);
// - |target|: WidgetDescription of the target; |reason|: why it refused.
enum class PressOriginRefusal {
	None,
	TopLevelTarget,
	NativeTarget,
	DisabledTarget,
	PointOutside,
	OriginUnreachable,
};

struct PressOrigin {
	bool pressSent = false;
	bool delivered = false;
	bool displaced = false;
	bool restored = false;
	bool pressAccepted = false;
	bool targetAlive = false;
	bool cursorStable = false;
	bool geometryChangedByPress = false;
	int moveEventsWithheld = 0;
	int paintsWhileDisplaced = 0;
	QPoint point;
	QPoint cursorBefore;
	QPoint cursorAfter;
	QPoint displacement;
	QPoint originSeen;
	QPoint pressGlobal;
	QString target;
	PressOriginRefusal refusal = PressOriginRefusal::None;
	QString reason;
};

PressOrigin ClickWithPressOrigin(
	not_null<QWidget*> widget,
	std::optional<QPoint> point = {});
[[nodiscard]] QString PressOriginRefusalName(PressOriginRefusal refusal);
[[nodiscard]] QString PressOriginDetails(const PressOrigin &reading);

// Synthesizes key press + release pairs carrying one Unicode grapheme at a
// time. Surrogate pairs and joined emoji are never split between events.
void TypeText(not_null<QWidget*> widget, const QString &text);

// Delivers one input-method commit. Prefer this when the behavior under test
// is text insertion itself rather than physical key handling.
void CommitText(not_null<QWidget*> widget, const QString &text);

// Synthesizes a left-button drag in widget-local coordinates. Intermediate
// mouse moves retain Qt::LeftButton in buttons(), matching a real drag.
// Like Click, it ends with a QEvent::Leave, so the drag leaves no hover
// behind.
//
// A drag whose start the target queues - both message lists queue
// performDrag() with InvokeQueued once the pointer leaves the pressed item
// or passes the drag distance - is released before that call runs, so the
// drag never starts. Such a drag needs Test::AppendQueuedStartDrag
// (test_queued_drag.h). Drag sends every event to |widget| itself,
// bypassing Qt's window routing, so moves never follow a grabMouse() made
// elsewhere - a child that forwards only its press to a grabbing parent.
// Such a gesture needs Test::AppendQpaMouseDrag (test_qpa_mouse.h).
void Drag(
	not_null<QWidget*> widget,
	QPoint from,
	QPoint to,
	int steps = 8,
	Qt::KeyboardModifiers modifiers = Qt::NoModifier);

// The two halves Drag is made of: Drag is DragPressAndMoves, then
// DragRelease when the first answered true (false means the widget died
// during the press or a move, and nothing more was sent). They deliver the
// same events through the same settle as Drag, and are exported for
// Test::AppendQueuedStartDrag (test_queued_drag.h), which returns to the
// event loop between them. A scenario uses Drag or that helper, never the
// halves by hand: a press and moves without their release leave the
// target's button down across turns.
[[nodiscard]] bool DragPressAndMoves(
	not_null<QWidget*> widget,
	QPoint from,
	QPoint to,
	int steps = 8,
	Qt::KeyboardModifiers modifiers = Qt::NoModifier);

// The release at |to| and the same QEvent::Leave as Click and Drag.
void DragRelease(
	not_null<QWidget*> widget,
	QPoint to,
	Qt::KeyboardModifiers modifiers = Qt::NoModifier);

// Synthesizes a wheel event at |point| in the named widget's coordinates
// (the widget centre by default). |angleDelta| uses Qt's native
// eighths-of-a-degree convention (120 is one wheel step).
//
// A stack-built QWheelEvent sent with QApplication::sendEvent is not
// spontaneous, and Qt 6's QApplication::notify therefore does not climb
// parentWidget() the way a real wheel does. The helper replays that
// ladder itself: a fresh event per widget, accepted reset by the
// constructor, continuing while sendEvent returns false or the event is
// ignored, stopping at isWindow() or Qt::WA_NoMousePropagation.
// QAbstractScrollArea::event returns false for Wheel without ignoring,
// so a delivery to the scroll area itself is recorded in |inert| and is
// never |delivered|. The viewport is the widget that actually consumes
// a wheel aimed at a covering child. The helper neither Notes nor Fails;
// the caller reads the return, as with WindowActivation.
struct WheelDelivery {
	bool delivered = false;
	QString receiver;
	QString inert;
	QString refusal;
};

WheelDelivery Wheel(
	not_null<QWidget*> widget,
	QPoint angleDelta,
	std::optional<QPoint> point = {});
[[nodiscard]] QString WheelDeliveryDetails(const WheelDelivery &reading);

// Hands |text| to a Ui::InputField the way a paste does, without the
// system clipboard. It exists because the clipboard read back empty right
// after setText on the Windows host of
// 2026/09/29/keep-the-focused-word-field-in-view-when-importing-a-wallet
// (work/test.md, Run 1, Tests 9 and 10), so every paste check there was
// left undecided.
//
// Route: a stack QMimeData carrying |text|, a QDragEnterEvent and then a
// QDropEvent, both sent to field->rawTextEdit()->viewport(). The viewport's
// QAbstractScrollAreaFilter hands them to QTextEdit::dragEnterEvent and
// InputField::dropEventInner, which reach canInsertFromMimeDataInner and
// insertFromMimeDataInner - so the field's setMimeDataHook, or the field's
// own insertion when the hook leaves it, which is where Ctrl+V also ends.
//
// Qt routing (qapplication.cpp, QApplication::notify): the enter must come
// first, because a Drop goes only to QDragManager's current target, which
// only an accepted DragEnter sets; with no target a Drop is discarded. An
// ignored DragEnter climbs parentWidget() to the first enabled ancestor
// that accepts drops (a chat's file-drop area, for instance), and the Drop
// then goes there. The helper therefore sends the drop only when the
// field's own viewport accepted the enter, and for the duration of the
// pair it installs an application event filter (a shield) that withholds
// every drag event from any receiver other than that viewport. No other
// widget ever sees the enter or the drop; |shielded| names every widget Qt
// would have offered them to. An enter that was not accepted is balanced
// with a QDragLeaveEvent to the viewport, as Qt itself does.
//
// The pair runs inside ONE Test::Settle, not one per event: an accepted
// enter makes the viewport QDragManager's raw (unguarded) current target,
// and a drain between the enter and the drop could run product code that
// destroys the field while the manager still points at it. The drop or the
// leave clears the target before any postponed call runs, and the single
// drain afterwards still leaves getLastText() settled.
//
// The text goes in at the caret (the drop point is the cursor rectangle's
// centre); unlike a paste, a selection is NOT replaced
// (QWidgetTextControl::dropEvent moves the cursor to the drop point).
//
// Not covered: it does NOT exercise the clipboard read of a Paste
// accessory (QGuiApplication::clipboard()->text() in its click callback)
// or of Ctrl+V (QTextEdit::paste). A check whose subject is that read
// still needs a host where the clipboard reads back. The helper neither
// reads nor writes the system clipboard.
//
// Side effects: InputField::dropEventInner calls window()->raise() and
// window()->activateWindow() after the drop. On macOS QCocoaWindow::raise
// also activates the application unless QT_MAC_SET_RAISE_PROCESS=0, so a
// shown field's window, and the app, come to the front; a never-shown
// top-level is unaffected. The viewport keeps Qt::WA_UnderMouse after a
// delivered drop (only Leave or DragLeave clears it), as a real drop under
// the pointer would.
//
// |delivered| is exactly enterAccepted && dropAccepted. A refusal is
// returned, never logged: the helper neither Notes nor Fails, and the
// caller judges the reading, as with WheelDelivery. |fieldAlive| reports a
// field destroyed by the final drain; that is not a refusal.
// TextDropDetails never prints the dropped text or the field's text,
// because a campaign may drop real secret words.
enum class TextDropRefusal {
	None,
	ViewportRefusesDrops,
	EnterNotAccepted,
	DropNotAccepted,
	Unsupported,
};

struct TextDrop {
	bool delivered = false;
	bool enterAccepted = false;
	bool dropSent = false;
	bool dropAccepted = false;
	bool fieldAlive = false;
	int textLength = 0;
	int fieldLengthBefore = 0;
	int fieldLengthAfter = 0;
	QString target;
	QStringList shielded;
	TextDropRefusal refusal = TextDropRefusal::None;
	QString reason;
};

[[nodiscard]] QString TextDropRefusalName(TextDropRefusal refusal);
TextDrop DropText(not_null<Ui::InputField*> field, const QString &text);
[[nodiscard]] QString TextDropDetails(const TextDrop &reading);

void PressKey(
	not_null<QWidget*> widget,
	int key,
	Qt::KeyboardModifiers modifiers = Qt::NoModifier);

// Performs the action with postponed-call processing deferred and a
// Test::HarnessInputScope open, then closes the scope and runs every
// pending Ui::PostponeCall to empty. The input helpers use it around each
// delivered event; wrap programmatic text mutations (InputField::setText),
// and any raw input delivery that must pass an armed console shield, in it.
void Settle(Fn<void()> action);

// Runs every pending Ui::PostponeCall (see the contract above) — the
// bare drain, with no defer around anything.
void SettlePostponedCalls();

// Injects window activation through the QPA seam the platform plugin itself
// reports through, so a focus-routed affordance can be driven on a console
// where the window is never active in Qt's sense.
//
// QWidget::setFocus() walks the focus_child chain unconditionally, but only
// its if (f->isActiveWindow()) branch promotes the target to
// QApplication::focusWidget(), and isActiveWindow() ends in a fallback to
// QPlatformWindow::isActive() (qwidget.cpp) - so it takes the
// platform window not being active, on a locked console, or on macOS after a
// default workspace.py test-run launch, which does not activate the client,
// for setFocus() to "succeed" while hasFocus() and isActiveWindow() keep
// reading false - no error, no event, just absence - and every affordance
// routed on them silently does nothing. Clearing only the QPA focus window
// does not reproduce that where the OS window is still active. Runs 2 and 7
// of 2026/08/30/replace-wallet-with-new-or-imported paid for that.
// Window::Controller::activate() (window/window_controller.h) is not the
// answer: it asks the window manager, which on a locked console does not
// comply, which is why the injection goes through the QPA seam instead.
//
// Run 7 added the second half: the platform de-activates the window again
// asynchronously, between event-loop turns. In that one run an activation
// used in the same synchronous block as its dependent action worked, while
// a one-shot activation read back across polled turns was already gone. So
// a one-shot arrangement expected to survive across polled turns is a
// forbidden technique - assert activation in the same turn as the action,
// and re-assert it on every poll of a bounded wait.
//
// Calling ForceWindowActive from a stage's until is a deliberate, narrow
// exception to the README's stage contract, which says a readiness must be
// pure: it mutates only which window Qt considers focused, never product
// state, it is idempotent and repeatable, and it encodes no expected
// product result. Its note is capped to every kActivationNoteEvery-th call,
// and |attempts| and |notes| are process-wide counts carried in every
// reading, so the flake stays visible without flooding a 50ms-tick log.
inline constexpr auto kActivationNoteEvery = 10;

// For the values ForceWindowActive and ClearWindowActive return, |injected|
// is true exactly when |refusal| is empty, and a refusal is returned rather
// than logged: only the caller knows whether a window the helper could not
// resolve is a failure of the run or an expected reading.
// ReadWindowActivation injects nothing, so it always answers injected=false
// and fills |refusal| only when there is no window to activate through -
// never read its |injected| as "activation is in force".
struct WindowActivation {
	bool injected = false;
	bool focusWindowSet = false;
	bool activeWindow = false;
	int attempts = 0;
	int notes = 0;
	QString identity;
	QString refusal;
};

[[nodiscard]] WindowActivation ReadWindowActivation(
	QWidget *widget = nullptr);
WindowActivation ForceWindowActive(QWidget *widget = nullptr);

// Deliberately clears the application's focus window through the same seam,
// so a self-test can reach the failing shape instead of describing it. Never
// call this from a scenario, and never leave a stage with it in force.
WindowActivation ClearWindowActive();

// The one formatter both the helper's own note and every self-test Check
// detail print, so a pass and a refusal carry the same fields. It takes the
// reading rather than re-reading, because |attempts| and |notes| move on
// every call and a re-read would print numbers no verdict was made against.
[[nodiscard]] QString WindowActivationDetails(
	const WindowActivation &reading);

} // namespace Test
