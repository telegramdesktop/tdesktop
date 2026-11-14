/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QPoint>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <memory>

class QWidget;
class QWindow;

namespace Test {

class Runner;

// Mouse input delivered the way the platform delivers it: through
// QWindowSystemInterface::handleMouseEvent<SynchronousDelivery> to the
// QWindow of a live widget's top-level, so Qt itself routes it -
// QWidgetWindow picks the receiver, a mouse grab or the pressed widget
// takes the held moves and the release
// (QApplicationPrivate::pickMouseReceiver) - and every event is
// spontaneous. Test::Click and Test::Drag send each event straight to the
// named widget and bypass that routing; use this module when the subject
// depends on it, such as a widget that calls grabMouse() on press.
// Reference: docs/helpers/test_qpa_mouse.md.
//
// Each delivery runs inside Test::Settle, so its postponed calls drain
// after it and an armed console shield passes it as harness input.
// Points are in the target's logical (device-independent) pixels: the
// window-local and global points are converted with
// QHighDpi::toNativeLocalPosition and toNativeGlobalPosition into the
// native coordinates QWindowSystemInterface takes. Timestamps are Qt's
// own. Before every press Qt's double-click pairing button
// (QGuiApplicationPrivate::mousePressButton) is reset, so a helper press
// never pairs into a double click, and Qt's last cursor position is set to
// the press point, so Qt synthesizes no move before it. Qt drops a move
// to the point it already holds (sent, not delivered), and precedes a
// release away from its last point with a move of its own.
//
// Preconditions the caller owns: no button held and no mouse grab when a
// sequence starts, no other gesture meanwhile, a shown ordinary widget
// window, no popup and no modal in the way. The helper never raises,
// activates or focuses a window and never moves the OS cursor; as with
// real input, a press may give focus to a ClickFocus widget inside its
// window. A caller whose handlers need an active window calls
// Test::ForceWindowActive first, outside Settle. A gesture that reaches
// QDrag::exec() starts a real platform drag: the consumer's obligation in
// test_queued_drag.h ("SAFETY") applies here as well. Restoring a drag or
// a grab that existed before the sequence is not promised.

// A refused delivery sent nothing. A refusal before a sequence's first
// delivery changes no pointer state.
// - unsupported: built with DESKTOP_APP_DISABLE_PRIVATE_QT_API; every
//   delivery and drag is refused so;
// - no-target: no widget was given (a drag's resolver answered none);
// - target-destroyed: the target died - before this delivery, or, for a
//   drag, during a delivery or between turns;
// - no-window-handle: the target's top-level has no QWindow (it was never
//   shown); the helper never calls winId() to create one;
// - window-hidden: the top-level or its QWindow is not visible, or the
//   top-level has Qt::WA_DontShowOnScreen, whose offscreen delivery path
//   keeps the pressed widget after the release;
// - active-popup: a popup is open, even in the target's own window, so Qt
//   would route the event through its popup path;
// - blocked-by-modal: a modal window blocks the target's window, so Qt
//   would drop the event, and its modal path may raise a window;
// - bound-expired: a drag's queued-work markers had not both run within
//   its bound; its release is still sent.
enum class QpaMouseRefusal {
	None,
	Unsupported,
	NoTarget,
	TargetDestroyed,
	NoWindowHandle,
	WindowHidden,
	ActivePopup,
	BlockedByModal,
	BoundExpired,
};

// none, unsupported, no-target, target-destroyed, no-window-handle,
// window-hidden, active-popup, blocked-by-modal, bound-expired.
[[nodiscard]] QString QpaMouseRefusalName(QpaMouseRefusal refusal);

enum class QpaMouseType {
	Press,
	Move,
	Release,
};

// One event. |point| is in the target's logical coordinates. |button| is
// the button pressed or released, Qt::NoButton for a move; |buttons| is
// what is held after the event, as QWindowSystemInterface takes it: a
// press includes |button|, a release no longer does, a held move carries
// the held buttons.
struct QpaMouseEvent {
	QpaMouseType type = QpaMouseType::Move;
	QPoint point;
	Qt::MouseButton button = Qt::NoButton;
	Qt::MouseButtons buttons = Qt::NoButton;
	Qt::KeyboardModifiers modifiers = Qt::NoModifier;
};

// One delivery. |sent|: the event went into QWindowSystemInterface;
// |delivered|: the target's QWindow received it; |accepted|: what
// handleMouseEvent answered. |receiver| is the first widget Qt sent that
// event to, described during the delivery; |grabber| is
// QWidget::mouseGrabber() after it; both are WidgetDescription texts,
// empty for none. |windowEvents| counts every mouse event the QWindow
// received during the call, Qt's synthesized move or double click
// included. |spontaneous| and |receiverSpontaneous| are the QWindow's and
// the first receiver's event flags. |local| and |global| are the logical
// window-local and global points, |nativeLocal| and |nativeGlobal| what
// QWindowSystemInterface was given. |buttons| is
// QGuiApplication::mouseButtons() after the delivery; |targetAlive| says
// whether the target survived it.
struct QpaMouseDelivery {
	QString receiver;
	QString grabber;
	QString reason;
	QPointF local;
	QPointF global;
	QPointF nativeLocal;
	QPointF nativeGlobal;
	Qt::MouseButtons buttons = Qt::NoButton;
	int windowEvents = 0;
	QpaMouseType type = QpaMouseType::Move;
	QpaMouseRefusal refusal = QpaMouseRefusal::None;
	bool sent = false;
	bool delivered = false;
	bool accepted = false;
	bool spontaneous = false;
	bool receiverSpontaneous = false;
	bool targetAlive = false;
};

// What QpaMouseSequence::end() did, then found. |held|: the buttons the
// sequence's own presses still held. |releaseSent|: they were released
// through the saved QWindow at the last delivered point, with the target
// alive or not (|targetAlive|), not part of the gesture. |buttonsCleared|:
// that window was gone, or a popup or a modal was in the way, so only
// those bits were cleared from Qt's mouse-buttons state and nothing was
// sent. |grabReleased|: a mouse grab that appeared after the first
// delivery was still held and was released. |leaveSent|: a
// QWindowSystemInterface leave was delivered to the window. Then
// |buttons| is QGuiApplication::mouseButtons(), |grabber|
// QWidget::mouseGrabber() and |hovered| the target window's widgets with
// underMouse(), as WidgetDescription texts.
struct QpaMouseEnd {
	QString grabber;
	QStringList hovered;
	Qt::MouseButtons held = Qt::NoButton;
	Qt::MouseButtons buttons = Qt::NoButton;
	bool releaseSent = false;
	bool buttonsCleared = false;
	bool targetAlive = false;
	bool grabReleased = false;
	bool leaveSent = false;
	bool ended = false;

	[[nodiscard]] bool clean() const {
		return ended && !buttons && grabber.isEmpty() && hovered.isEmpty();
	}
};

// "<press|move|release> sent=<0|1> delivered=<0|1> accepted=<0|1>
// windowEvents=<n> spontaneous=<0|1> receiver=<description|none>
// receiverSpontaneous=<0|1> grabber=<description|none> buttons=<int>
// targetAlive=<0|1> local=<x>,<y> global=<x>,<y> nativeLocal=<x>,<y>
// nativeGlobal=<x>,<y> refusal=<name>", and " - <reason>" with one.
[[nodiscard]] QString QpaMouseDeliveryText(const QpaMouseDelivery &reading);

// "ended=<0|1> held=<int> releaseSent=<0|1> targetAlive=<0|1>
// buttonsCleared=<0|1> grabReleased=<0|1> leaveSent=<0|1> buttons=<int>
// grabber=<description|none> hovered=[<description>; ...] clean=<0|1>".
[[nodiscard]] QString QpaMouseEndText(const QpaMouseEnd &reading);

// One sequence of deliveries to one target, in the caller's own order, in
// one turn or across turns. It keeps QPointers to the target, its
// top-level and that top-level's QWindow, the buttons its presses hold and
// the last delivered native points, so end() needs neither a live target
// nor another mapping.
//
// end() is the caller's obligation, from Runner::onFinish too when the
// sequence can outlive its stage. It releases the buttons the sequence's
// presses still hold, through the saved QWindow while it lives and no
// popup or modal is in the way, else by clearing just those bits; releases
// a grab that appeared after the first delivery; delivers a
// QWindowSystemInterface leave - each inside Settle - and reads the end
// state. A sequence that never sent anything changes nothing. Called again
// with no delivery in between, end() sends nothing and returns the same
// reading; a delivery after it opens the sequence again. The destructor
// delivers nothing. Left behind by design: Qt's last cursor position (the
// last delivered point), and Qt's current mouse window and last mouse
// receiver, which the leave clears.
class QpaMouseSequence final {
public:
	explicit QpaMouseSequence(QWidget *target);
	QpaMouseSequence(const QpaMouseSequence &other) = delete;
	QpaMouseSequence &operator=(const QpaMouseSequence &other) = delete;
	~QpaMouseSequence();

	// Refuses by name, or delivers |event| synchronously inside Settle.
	QpaMouseDelivery deliver(const QpaMouseEvent &event);
	QpaMouseEnd end();

	// The buttons this sequence's presses hold now.
	[[nodiscard]] Qt::MouseButtons held() const;

private:
	QPointer<QWidget> _target;
	QPointer<QWidget> _top;
	QPointer<QWindow> _window;
	QPointer<QWidget> _grabberBefore;
	QPointF _lastNativeLocal;
	QPointF _lastNativeGlobal;
	QpaMouseEnd _end;
	Qt::MouseButtons _held = Qt::NoButton;
	Qt::KeyboardModifiers _modifiers = Qt::NoModifier;
	bool _hadTarget = false;
	bool _sent = false;
	bool _open = false;

};

inline constexpr auto kQpaMouseDragBound = crl::time(3000);

// What the resolver answers when the stage begins: the widget that takes
// the press, and the press and release points in its logical coordinates.
// A null |widget| is refused no-target.
struct QpaMouseDragTarget {
	QWidget *widget = nullptr;
	QPoint from;
	QPoint to;
};

// |name| labels the stage ("QPA mouse drag: <name>") and the row. |steps|
// held moves (at least one) go from |from| to |to| at Test::Drag's points,
// from + (to - from) * i / steps. With |paceMs| above zero one move is
// delivered per |paceMs| ms from a timer, each in its own event-loop turn,
// instead of all in |run|. |bound| is how long the stage waits for the
// queued-work markers.
struct QpaMouseDragArgs {
	QString name;
	Fn<QpaMouseDragTarget()> resolve;
	Fn<QString()> skipReason;
	int steps = 8;
	crl::time paceMs = 0;
	Qt::KeyboardModifiers modifiers = Qt::NoModifier;
	crl::time bound = kQpaMouseDragBound;
};

// The reading the stage fills. |name|, |bound|, |paceMs| and |steps| are
// filled when the stage is appended, so a timeout's details print them.
// |pressed|: the press reached the target's QWindow; |moves|: how many
// moves did; |pressReceiver|, |grabber| and |releaseReceiver| are the
// press's first receiver, the grabber after the press and the release's
// first receiver. |qtQueueRan|, |mainQueueRan| and |waitedMs| are the
// markers as |then| found them. |released|: the gesture's own release
// reached a live target's window - a cleanup release by end() is reported
// in |end| instead. |aborted|: the runner finished before |then| and its
// Runner::onFinish ended the sequence. |finished| stays false for a
// skipped or aborted stage. |refusal| is the first one met.
struct QpaMouseDrag {
	QString name;
	QString target;
	QString pressReceiver;
	QString grabber;
	QString releaseReceiver;
	QString reason;
	QPoint from;
	QPoint to;
	QpaMouseEnd end;
	crl::time bound = 0;
	crl::time paceMs = 0;
	crl::time waitedMs = 0;
	int steps = 0;
	int moves = 0;
	QpaMouseRefusal refusal = QpaMouseRefusal::None;
	bool pressed = false;
	bool qtQueueRan = false;
	bool mainQueueRan = false;
	bool released = false;
	bool aborted = false;
	bool finished = false;

	[[nodiscard]] bool delivered() const {
		return finished && (refusal == QpaMouseRefusal::None);
	}
};

// "QPA_MOUSE_DRAG: name=<name> finished=<0|1> aborted=<0|1>
// target=<description|none> from=<x>,<y> to=<x>,<y> steps=<n>
// paceMs=<ms> bound=<ms> waitedMs=<ms> pressed=<0|1> moves=<n>
// qtQueue=<0|1> mainQueue=<0|1> released=<0|1>
// pressReceiver=<description|none> grabber=<description|none>
// releaseReceiver=<description|none> refusal=<name> end=[<end text>]",
// and " - <reason>" with one.
[[nodiscard]] QString QpaMouseDragText(const QpaMouseDrag &reading);

// Appends the stage "QPA mouse drag: <name>" and returns the reading it
// fills, a left-button drag through one QpaMouseSequence. Its |run|
// resolves the target and points, delivers the press, then the moves -
// all at once, or paced. After the last move's settle it posts
// Test::PostQueuedWorkMarkers(target) (test_queued_drag.h, where "The
// guarantee" says what their running establishes). Its |until| is pure:
// the gesture stopped on a refusal, the target is gone, both markers ran
// or |bound| ms passed since they were posted. Its |then| stops the pace
// timer, reads the markers, sends the release to a live target - after a
// bound-expired refusal too - calls end(), freezes the reading and Notes
// one QPA_MOUSE_DRAG row. A refusal stops the gesture and is never
// replaced by a later one or by the wait's outcome; the target's death is
// checked after every delivery and between turns. A Runner::onFinish
// registered here stops the pace timer and ends a sequence the stage left
// open, marking that reading aborted. Nothing writes a reading after its
// |then|. The stage's timeout is |bound| plus |steps| x |paceMs| plus
// kDefaultStageTimeout, so the helper's own refusal comes first. The
// helper never Passes or Fails. Read the reading from a later stage and
// treat a refusal as a fixture gate.
[[nodiscard]] std::shared_ptr<const QpaMouseDrag> AppendQpaMouseDrag(
	not_null<Runner*> runner,
	QpaMouseDragArgs args);

// The helper measuring itself on purpose-built probe widgets in shown
// ordinary top-level windows, with no account, chats list or network:
// grab routing and the exact sequence against a Test::Drag control,
// queued work run before the release, the release-started animation, Run
// 1's ingredients (direct QWindowSystemInterface legs with only the
// settle, only the queued-work wait or neither, logged and not judged),
// coordinates against an unconverted control (discriminating only at a
// QT_SCALE_FACTOR other than 1), no double click against a stamped
// positive control, every refusal, a paced gesture under its own armed
// console shield (test_console_shield.h) with no veto, and a clean end
// state after each leg. It arms and releases that shield itself, so it is
// appended before AppendConsoleShieldSelfTest, which comes last. Defined
// in test_qpa_mouse_self_test.cpp. Reference:
// docs/helpers/test_qpa_mouse_self_test.md; measured results:
// docs/helpers/test_qpa_mouse.md, "Verification".
void AppendQpaMouseSelfTest(not_null<Runner*> runner);

} // namespace Test
