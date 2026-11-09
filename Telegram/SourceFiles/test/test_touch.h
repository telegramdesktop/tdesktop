/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QEvent>
#include <QtCore/QPointF>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtGui/QEventPoint>
#include <QtGui/QInputDevice>

#include <optional>
#include <vector>

class QPointingDevice;
class QWidget;
class QWindow;

namespace Test {

class Runner;

// Touch input delivered through Qt's platform layer, from a touchscreen and
// a touchpad the harness registers once per process, so the events reach
// widgets through Qt's own touch routing, implicit grabs and mouse
// synthesis, as a real touchscreen's events do.
//
// Devices. The first HarnessTouchDevice call registers both devices with
// QWindowSystemInterface::registerInputDevice: a
// QInputDevice::DeviceType::TouchScreen and a TouchPad, each a
// QPointingDevice with PointerType::Finger, up to 10 contacts, its own
// systemId (QInputDevicePrivate::isRegistered compares systemId() only)
// and the application as its QObject parent. Every later call returns the
// same pointers: a device is never replaced, not after a cancel and not for
// another leg. The screen has Position | Area and no MouseEmulation, so Qt
// synthesizes mouse input (Qt::MouseEventSynthesizedByQt, carrying the
// screen device) from a sequence no widget accepted; the pad has Position |
// NormalizedPosition | MouseEmulation, so Qt never synthesizes mouse from
// it. QPointingDevice::primaryPointingDevice() runs before either is
// registered, as lib_ui's TouchpadDevice() in
// ui_windows_direct_manipulation.cpp does: it creates the "core pointer"
// mouse only while no mouse and no touchpad is registered, the Windows
// platform plugin registers no mouse, and a TouchPad registered first would
// become the primary pointing device every ordinary mouse event is
// attributed to. IsHarnessTouchDevice compares a device with the two
// registered identities and registers nothing. With
// DESKTOP_APP_DISABLE_PRIVATE_QT_API nothing is registered,
// HarnessTouchDevice answers null and every delivery is refused
// "unsupported".
//
// Frames. A TouchSequence is ONE gesture of one device, bound when it is
// constructed to a target widget, whose local logical coordinates every
// position is given in. send(changes) delivers one complete frame: the
// listed contacts in the caller's order, then every other contact of this
// sequence still down, in press order, as Stationary. A Pressed or Updated
// contact needs a position; a Stationary one carries none and keeps its
// stored position; a Released one without a position is released where it
// last was. An empty list while contacts are down submits an all-Stationary
// frame: Qt delivers it to the QWindow as a TouchUpdate and, in
// QApplicationPrivate::translateRawTouchEvent, sends no widget event for
// it - Qt drops it, the helper does not. No widget took that QWindow
// event, so for the screen QGuiApplicationPrivate::processTouchEvent
// synthesizes mouse from it as from any frame no widget accepted: a
// MouseMove with Qt::LeftButton held reaches the QWindow, and
// QApplicationPrivate::pickMouseReceiver gives it to no widget while no
// button is down. The window then stays in Qt's synthesized-mouse set, and
// the next touch cancel sends it a mouse release
// (Qt::MouseEventNotSynthesized), which no widget gets either. Released
// contacts leave the sequence after the frame is submitted. When the last
// one is released the
// gesture is finished; the next gesture is a new TouchSequence on the same
// registered device. A frame that breaks these rules - a listed id twice, a
// negative id, a Pressed id already down, any other state for an id not
// down, Unknown or a combined state, a missing or non-finite position, a
// position on a Stationary contact, more contacts down than the device's
// maximumPoints(), or an empty list with nothing down - is refused
// "invalid-frame" before anything changes or is sent.
//
// Delivery. Positions map to global logical coordinates with
// QWidget::mapToGlobal(QPointF), become QEventPoints, pass through
// QWindowSystemInterfacePrivate::toNativeTouchPoints and go to
// QWindowSystemInterface::handleTouchEvent<SynchronousDelivery> for the
// target's top-level QWindow, which the first submitted frame binds. Qt
// converts logical to native and back itself; the helper never scales or
// rounds a coordinate and never sends a QTouchEvent to a widget. cancel()
// calls handleTouchCancelEvent<SynchronousDelivery> with no points and
// ends the sequence's own contacts; it is refused "nothing-down" while no
// contact of the sequence is down. |submitted| is the QPA call's answer:
// Qt Gui took the frame. It is no proof that a widget accepted anything -
// QGuiApplicationPrivate::processTouchEvent never clears that event's
// accepted flag - so read acceptance from the rows and the owner.
//
// Settling and lifetime. Each submission runs inside Test::Settle, so the
// Ui::PostponeCall work it queued has run when send() or cancel() returns.
// The target and the bound QWindow are held as QPointers. Losing either
// stops the sequence (|stopped|) - in the dispatch, in its drain, in the
// call's own cleanup below or between two calls - and a stopped sequence
// submits no frame again; nothing revives it. Every call takes its
// |targetAlive| and |windowAlive| last, after all of its cleanup and every
// drain, so a target or window a cleanup Leave destroyed already reads
// dead, and has stopped the sequence, when the call returns. The
// destructor sends nothing: releasing or cancelling is the caller's
// contract, and Qt keeps holding any contact a dropped sequence left down.
//
// Terminal cleanup, the one thing the helper submits unasked. A target
// that refuses the touch can take the mouse press Qt synthesizes from it
// and then die; or another contact's widget accepts the frame that
// releases every contact, and QGuiApplicationPrivate::processTouchEvent
// synthesizes no release for that press at all. Either way
// QGuiApplication::mouseButtons() stays Qt::LeftButton and a widget that
// took the press stays down: neither a Leave nor the widget's destruction
// releases it, a later accepted sequence synthesizes no release, and the
// ended sequence refuses the release or cancel that would. So when the
// sequence stops, or its gesture ends with the last release or a cancel,
// while its own rows hold a Qt::MouseEventSynthesizedByQt press from its
// device at a QWindow that no mouse release from that device followed,
// and the bound QWindow lives, the call submits one point-free
// handleTouchCancelEvent<SynchronousDelivery> inside Test::Settle - for a
// stop before the ending's Leave pass, for a gesture that only ended after
// it - and processTouchEvent then sends that window the mouse release
// (Qt::MouseEventNotSynthesized, the screen device) that balances its
// synthesized press, and the window hands it to the widget still holding
// the press, or to none; once every contact was released Qt holds no
// point to cancel and sends no window a TouchCancel, only that release. A
// cancel() of a gesture holding the press makes processTouchEvent send
// that release itself to every window it synthesized mouse in, so the
// cleanup then finds no press held. It is tried once per sequence;
// |cleanupCancel| and |cleanupRows| record it. A gesture that ended
// without a loss stays finished and not stopped, and its next call is
// refused "finished" with nothing sent. A frame refused for a loss stays
// unsent, no widget gets an event the helper made up, and no other input
// state is reset. With the window gone as well Qt has no window to release
// in, and the press stays.
//
// Rows. For each dispatch and its drain one application event filter
// copies, and never vetoes: every touch event with its receiver, the
// QWindow rows included; every mouse press, release, double click and move
// of any source and any device, because Ui::ScrollArea's own tap and
// scroll moves carry the primary pointing device and
// Qt::MouseEventSynthesizedByApplication (and its moves are aimed at the
// real QCursor::pos()); and every Enter a widget receives. Values are
// copied when the filter sees the event: Qt rewrites a TouchBegin's point
// positions while it climbs, and QEventPoint is explicitly shared. A row
// keeps its receiver as a QPointer beside a label made at that moment, so
// every reading stays printable after its widgets are gone.
//
// The owner: the accepted-Begin rule. QApplication::notify offers a
// TouchBegin to the target and then up its ancestors, but only to widgets
// with Qt::WA_AcceptTouchEvents (only they reach the filter), and stops at
// the first one that accepts it, which gets the implicit grab. It sets
// Qt::WA_WState_AcceptedTouchBeginEvent on every climbed widget to whether
// that one accepted - and then
// QApplicationPrivate::translateRawTouchEvent sets the attribute again on
// the original target, although it refused. Walking up from the first
// receiver to the first marked widget therefore names a widget that
// refused. owner() instead reads the last widget the sequence's initial
// Begin climb was offered to, right after the QPA call returned, before
// the drain and before any End clears the attribute; it stays cached after
// End or the widget's death. Begin rows are grouped into climbs by equal
// contact ids and each offer going to an ancestor of the previous one, so
// independent Begin groups of one frame are never joined; |climb| numbers
// them, 1 being the initial climb. The owner's |status| is Accepted when
// that widget carries the attribute (|widget| is it), Refused when it does
// not or is gone, and None while no Begin reached a widget. It is
// Ambiguous when a later climb of the same frame was offered that widget
// too: translateRawTouchEvent builds every Begin group of a frame before
// it sends any, and each offer rewrites the attribute, so what remains is
// the last climb's answer - two contacts on two refusing siblings whose
// common ancestor refuses one Begin and accepts the other leave it set
// whichever group it refused, and when it accepts first, its second answer
// unmarks it, so Qt sends it no Update or End of the group it accepted. An
// Ambiguous owner has no |widget|, and its |label| names the shared
// widget. Further climbs stay in the rows only: several independent
// targets have no single owner.
//
// Hover. When the gesture ends - the last release, a cancel, or a stop,
// after a stop's cleanup - every widget that received a mouse event or an
// Enter during the sequence and is still alive gets a QEvent::Leave
// inside Test::Settle, each checked alive right before its own delivery,
// so the README's "no helper leaves a hover behind" holds; |left| names
// them. A widget holding a press keeps its hover through that Leave -
// Ui::AbstractButton ignores a Leave while it is down - so when the
// terminal cleanup's release comes only after those Leaves, because the
// gesture ended with the synthesized press still held or their drain
// destroyed the target while it was, every surviving receiver, the
// cleanup's own included, gets the Leave once more after that release,
// and |left| names it once per pass. The rows keep the Enter and mouse
// events that happened. Qt's own last-mouse-receiver bookkeeping is not
// reset.
//
// Spontaneity. Every touch frame reaches its QWindow spontaneously
// (QGuiApplication::sendSpontaneousEvent). A widget hit directly that
// accepts touch reads a spontaneous Begin; any widget offered the Begin
// after the climb passed another widget - offered or skipped for lacking
// Qt::WA_AcceptTouchEvents - reads it non-spontaneous. Update, End and
// Cancel reach their widgets spontaneously. Mouse Qt synthesizes from the
// screen is spontaneous and carries the screen device; mouse a widget
// synthesizes itself carries the primary pointing device; Enter and Leave
// are not spontaneous. So an application-wide filter tells this helper's
// touch, and the mouse Qt synthesizes from it, apart by device
// (IsHarnessTouchDevice(event->device())), never by the spontaneous flag;
// Qt::MouseEventSynthesizedByApplication says only that a widget made the
// event, not that the harness caused it.
//
// Qt's retained points. A cancel does not remove the device's points:
// QPointingDevicePrivate::activePoints keeps each one with its grab
// target, and QGuiApplicationPrivate::processTouchEvent drops every touch
// frame of any device that is not a TouchBegin until the next one. A fresh
// sequence of the same device on the same receiver recovers; a same-id
// Begin on another widget is offered there while its Update and End still
// go to the earlier receiver, and a held pad point with a live target
// captures every later pad Begin. While QApplication::activePopupWidget()
// is set, QWidgetWindow::handleTouchEvent ignores touch, so Qt synthesizes
// mouse instead, and processTouchEvent drops touch for a window a modal
// window blocks (its End arrives there as a TouchCancel).
//
// Preconditions: the GUI thread; no nested input delivery (never call a
// sequence from an event handler or a postponed call it triggers); at most
// one unfinished sequence per device; and every gesture ends with its
// release or cancel before the sequence is abandoned. The helper neither
// Notes nor Fails; the caller judges the frames and the owner.
enum class TouchDeviceKind {
	Screen,
	Pad,
};

// Registers both harness devices on the first call (GUI thread only).
[[nodiscard]] const QPointingDevice *HarnessTouchDevice(TouchDeviceKind kind);

// True for exactly the two registered harness devices; registers nothing.
[[nodiscard]] bool IsHarnessTouchDevice(const QInputDevice *device);

// One requested contact change; |position| is in the sequence target's
// local logical coordinates.
struct TouchContact {
	int id = 0;
	QEventPoint::State state = QEventPoint::State::Unknown;
	std::optional<QPointF> position;
};

// A point as copied: in a frame's |sent| list, |position| is in the
// target's coordinates and |globalPosition| is what went to Qt's
// conversion; in a row, both are the event's own values at filter time.
struct TouchEventPoint {
	int id = 0;
	QEventPoint::State state = QEventPoint::State::Unknown;
	QPointF position;
	QPointF globalPosition;
};

// One event the filter saw. |window| marks a QWindow receiver. |device| is
// the event's input device and |deviceName| its name ("harness
// touchscreen", "harness touchpad", Qt's "core pointer", ...). Touch rows
// fill |points|; mouse and Enter rows fill |position| and
// |globalPosition|, and mouse rows |source|, |button| and |buttons|.
// |climb| numbers a widget's TouchBegin row's climb within the sequence and
// is 0 on every other row.
struct TouchEventRow {
	QPointer<QObject> receiver;
	QString label;
	QPointer<const QInputDevice> device;
	QString deviceName;
	std::vector<TouchEventPoint> points;
	QPointF position;
	QPointF globalPosition;
	QEvent::Type type = QEvent::None;
	Qt::MouseEventSource source = Qt::MouseEventNotSynthesized;
	Qt::MouseButton button = Qt::NoButton;
	Qt::MouseButtons buttons = Qt::NoButton;
	int climb = 0;
	bool window = false;
	bool spontaneous = false;
};

// One send() or cancel() call. |sent| is the complete frame handed to Qt
// (empty for a cancel or a refusal) and |rows| what Qt delivered during
// its dispatch and drain. |submitted| is the QPA call's answer; |refusal|
// names why nothing was submitted - unsupported, stopped, finished,
// target-gone, window-gone, no-window, invalid-frame, nothing-down or
// not-submitted - and |reason| explains it. |cleanupCancel| is the answer
// of the terminal cleanup's touch cancel, when this call stopped or ended
// the sequence with its synthesized press still held, and |cleanupRows|
// what Qt delivered for it. |targetAlive| and |windowAlive| are read last,
// after the dispatch, the terminal cleanup, the ending's Leave passes and
// every drain (the window reads false until a frame binds it). |left|
// names the widgets each of the ending's QEvent::Leave passes reached.
struct TouchFrame {
	std::vector<TouchEventPoint> sent;
	std::vector<TouchEventRow> rows;
	std::vector<TouchEventRow> cleanupRows;
	QStringList left;
	QString refusal;
	QString reason;
	bool cancel = false;
	bool submitted = false;
	bool cleanupCancel = false;
	bool targetAlive = false;
	bool windowAlive = false;
};

enum class TouchOwnerStatus {
	None,
	Accepted,
	Refused,
	Ambiguous,
};

// The initial Begin climb's acceptor (the owner rule above). |frame|
// indexes frames() for the frame that carried that climb and stays -1
// while |status| is None. |widget| is set only for Accepted and reads null
// once the owner is destroyed, while |label| still prints it; for
// Ambiguous |label| names the widget several climbs shared, and for
// Refused it is empty.
struct TouchOwner {
	QPointer<QWidget> widget;
	QString label;
	TouchOwnerStatus status = TouchOwnerStatus::None;
	int frame = -1;
};

class TouchSequence final {
public:
	explicit TouchSequence(
		not_null<QWidget*> target,
		TouchDeviceKind kind = TouchDeviceKind::Screen);
	TouchSequence(const TouchSequence &other) = delete;
	TouchSequence &operator=(const TouchSequence &other) = delete;

	TouchFrame send(const std::vector<TouchContact> &changes);
	TouchFrame cancel();

	// Every call so far, refusals included, in order.
	[[nodiscard]] const std::vector<TouchFrame> &frames() const;
	[[nodiscard]] const TouchOwner &owner() const;
	// The last contact was released or the sequence was cancelled.
	[[nodiscard]] bool finished() const;
	// The target or the bound window was destroyed.
	[[nodiscard]] bool stopped() const;

private:
	struct DownContact {
		int id = 0;
		QPointF global;
	};

	[[nodiscard]] bool ready(TouchFrame &frame);
	[[nodiscard]] QString invalid(
		const std::vector<TouchContact> &changes) const;
	[[nodiscard]] QWindow *bindWindow();
	void fillSent(
		TouchFrame &frame,
		const std::vector<TouchContact> &changes) const;
	void submit(
		TouchFrame &frame,
		Fn<bool(not_null<QWindow*>)> dispatch);
	void applySent(const std::vector<TouchEventPoint> &sent);
	void trackPress(const std::vector<TouchEventRow> &rows);
	void conclude(TouchFrame &frame);
	void readAlive(TouchFrame &frame) const;
	void latchLoss(TouchFrame &frame);
	void releasePress(TouchFrame &frame);
	void releaseHover(TouchFrame &frame);
	TouchFrame record(TouchFrame &&frame);

	const QPointer<QWidget> _target;
	const QPointingDevice *_device = nullptr;
	QPointer<QWindow> _window;
	std::vector<DownContact> _down;
	std::vector<TouchFrame> _frames;
	TouchOwner _owner;
	int _climbs = 0;
	bool _windowBound = false;
	bool _finished = false;
	bool _stopped = false;
	bool _pressHeld = false;
	bool _hoverReleased = false;

};

// The one formatter: a frame's kind, flags, sent points, rows, left
// widgets, terminal cleanup (when it ran) and refusal, with coordinates at
// three decimals.
[[nodiscard]] QString TouchFrameDetails(const TouchFrame &frame);

// The helper measuring itself on purpose-built widgets and a
// Ui::ScrollArea in one parentless harness window, plus a second small one
// for the leg that destroys its window, both shown without activation
// (Qt::WA_ShowWithoutActivating, Qt::WA_QuitOnClose off) and released
// through Runner::onFinish on every path; it needs no session, network or
// account. Geometry is in st::defaultActiveButton.height rows, and the
// sample points carry .25 / .75 fractions, input samples that expose any
// integer rounding on the path. Each probe copies every touch, mouse and
// Enter event in its own event(), with its own answer to a scripted Begin,
// and object filters on the main window's QWindow and on the held legs'
// Ui::AbstractButton copy their touch and mouse events, the button's Enter
// and Leave too: that journal, the probes' destructors and Qt's own
// QGuiApplication::mouseButtons(), never the helper's own rows, are the
// oracle, and each expectation is a literal list written from the samples.
// Each leg is delivered synchronously in its stage's run and judged in its
// then. The stages, in order:
// - fixture and devices: fixture gates - both windows have their QWindow,
//   every sample point hits the widget it is meant for, the scroll range is
//   longer than the swipe, no popup or modal window is up and
//   Qt::AA_SynthesizeMouseForUnhandledTouchEvents is set - then exactly
//   the two harness devices with their roles and capabilities, and a
//   primary pointing device that is neither. A failed gate makes every
//   judging stage N/A beside its FAIL row;
// - accepting probe: Begin, three Updates and End, each within 0.01 px of
//   the requested position at the printed device pixel ratio, the direct
//   Begin spontaneous; the helper's rows equal the journal; every frame
//   reached the QWindow once, spontaneously, from the screen; no widget
//   and no window got a mouse event, no widget an Enter, and nothing was
//   left; owner() is the probe, Accepted;
// - climb from a non-accepting child: a sequence bound to a child without
//   Qt::WA_AcceptTouchEvents reaches the accepting ancestor at the child's
//   points plus its offset, the child never offered, the climbed Begin
//   not spontaneous;
// - owner under a refusing child: the Begin is offered to the refusing
//   child, spontaneously, then in the same climb to the accepting probe;
//   both carry Qt::WA_WState_AcceptedTouchBeginEvent right after it;
//   owner() names the probe while the first-marked walk names the child;
//   Update and End reach only the probe, and the cached owner still names
//   it after End cleared its attribute;
// - unhandled control: a probe that ignores every touch gets
//   Qt::MouseEventSynthesizedByQt mouse from the screen, release
//   included, after its one refused Begin; owner() Refused, no widget;
//   QGuiApplication::mouseButtons() is Qt::NoButton afterwards and the
//   helper's Leave left the probe without hover;
// - accepting probe after the control: the accepting leg again, clean;
// - cancel and recovery: a point-free TouchCancel reaches the QWindow and
//   the probe, then a fresh sequence of the same device and id on the same
//   probe is delivered as Begin, Update and End, the device neither
//   replaced nor registered again;
// - multi-contact frames: P1, P2, M1, M2+S1, S1+S2, R1, R2 reach the probe
//   as exactly Begin{P1}, Update{P2,S1}, Update{M1,S2}, Update{M2,S1},
//   nothing, Update{R1,S2}, End{R2}; every frame reached the QWindow, the
//   all-Stationary one as a TouchUpdate; the one mouse event of the leg is
//   Qt's MouseMove from that frame at the QWindow, which no widget got;
// - touchpad contacts: two pad contacts reach the probe together as Begin,
//   Update and End from the pad, with no mouse;
// - postponed destruction: a probe whose Begin posts its own deletion with
//   Ui::PostponeCall is gone when send() returns, the sequence stopped and
//   its frame and owner still printable; the next send sends nothing; a
//   fresh sequence of the same device and id on a surviving probe is
//   delivered;
// - terminal cleanup after a synthesized press: a refusing probe that
//   takes Qt's synthesized press and posts its own deletion from it is
//   gone when send() returns, the sequence stopped, and the stop cleanup's
//   one point-free TouchCancel made Qt send the QWindow its release (not
//   synthesized, from the screen) and nothing to any widget, so
//   QGuiApplication::mouseButtons() is Qt::NoButton on return; then a
//   refusing probe deleted between two calls, while that press held
//   Qt::LeftButton, makes the next call refuse "target-gone" with the same
//   cleanup and Qt::NoButton after it; the call after each stop is refused
//   "stopped" and sends nothing; then the held gesture, bound to a
//   coordinate probe, transparent for mouse events, over a
//   Ui::AbstractButton that refuses touch and an accepting probe: contact
//   1 on the button gets Qt's synthesized press, contact 2's accepted
//   Begin and End keep Qt from synthesizing its release, and the gesture
//   ends with that press held and every widget alive; the terminal
//   cleanup's cancel then sends no TouchCancel and the release only to the
//   QWindow and the button, whose own record reads Leave, release, Leave;
//   on return the button is neither down nor isOver(), Qt's buttons are
//   released, the coordinate probe and the window live, and the sequence
//   is finished, not stopped; its next call is refused "finished" and
//   sends nothing; then the accepting leg again, a clean fresh gesture;
// - Leave cleanup destroying the target or its window: a refusing probe
//   armed to post the deletion of itself, then one armed to post the
//   deletion of the second window, from the Leave the helper sends when
//   its last release ends the gesture; the release returns
//   targetAlive=0, windowAlive=1 and then 0, with the sequence stopped as
//   well as finished, Qt's buttons released and nothing left to cancel;
//   then the held gesture again, with the coordinate probe armed so that
//   the button's Leave, while it is down, posts its deletion; the stop
//   cleanup's cancel then sends no TouchCancel and the release only to the
//   QWindow and the button, whose own record reads Leave, release, Leave;
//   on return the coordinate probe is gone, the button is neither down
//   nor isOver(), Qt's buttons are released, the window lives and the
//   sequence is stopped; the next call of each is refused "stopped" and
//   sends nothing;
// - owner across independent Begin groups: two contacts pressed together
//   on two refusing children of a common ancestor that refuses its first
//   Begin and accepts its second, then the reverse; the ancestor's own
//   answers and its attribute after the frame show the attribute is the
//   second group's answer, not the initial group's, the rows hold the two
//   climbs, child then ancestor, and owner() is Ambiguous with no widget;
//   unmarked by its second answer, the ancestor of the reverse never gets
//   the End of the group it accepted, and the release Qt synthesizes
//   instead reaches the QWindow only;
// - devices after every probe leg: the same two devices, primary neither;
// - scroll area tap: a tap on the Ui::AbstractButton inside the
//   Ui::ScrollArea clicks it exactly once through the press and release
//   the scroll area synthesizes itself (Qt::MouseEventSynthesizedByApplication,
//   the primary pointing device), with no Qt-synthesized mouse; the
//   content widget owns it; the button is neither isOver() nor
//   underMouse() after the helper's Leave;
// - scroll area swipe: a vertical swipe from the same point, mapped
//   through the viewport so the moving content cannot bend it, raises
//   scrollTop() and clicks nothing; the scroll area is released in the
//   same run, before its kinetic timer moves anything;
// - teardown: the window and its QWindow are gone.
// The scroll legs keep their Enter and application-synthesized mouse rows
// in view, and the refusing legs their Qt-synthesized mouse; only the
// accepted purpose-built probe legs must have none. It emits no deliberate
// failure. Select it from the disposable Test::SetupScenario slot after
// runner->waitEvent(u"launch_finished"_q).
void AppendTouchSelfTest(not_null<Runner*> runner);

} // namespace Test
