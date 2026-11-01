/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QPoint>
#include <QtCore/QString>

#include <memory>

class QObject;
class QWidget;

namespace Test {

class Runner;

// A left-button drag whose start the target queues: the press and the
// interpolated moves are delivered exactly as Test::Drag delivers them,
// then the event loop turns until the calls those events queued have run,
// and only then come the release and the same QEvent::Leave.
//
// Why it exists. In
// 2026/10/12/keep-the-forwarded-from-line-in-copied-message-selections
// (work/test.md, Attempt 1, Run 1, the reconciliation item "T11
// instrument") Test::Drag could not carry a drag out of a message list.
// HistoryInner::mouseActionUpdate() and
// HistoryView::ListWidget::mouseActionUpdate() turn
// MouseAction::PrepareDrag into MouseAction::Dragging once the pointer
// leaves the pressed item or passes QApplication::startDragDistance(), and
// queue performDrag() with InvokeQueued, a Qt queued metacall. Test::Drag
// delivers the release in the same synchronous call and drains only
// Ui::PostponeCall, so the release reaches mouseActionFinish(), which
// resets the action to MouseAction::None, before the queued call runs;
// prepareDrag() then answers null unless the action is still Dragging, and
// no drag starts - with no error and no refusal. The convergence note had
// assumed that settling after each move would run performDrag() during the
// moves; it does not, and the run drove the drag by hand in exactly the
// order this helper now owns. Info::Media::ListWidget::mouseActionUpdate()
// (shared media) and Dialogs::InnerWidget::mouseMoveEvent() (dragging a
// chat onto a folder) queue their performDrag() the same way, and their
// releases clear the state it checks. Ui::FlatLabel::dragActionUpdate()
// (lib_ui) queues executeDrag() the same way once a press on a link (one
// whose handler does not allow text selection) or on selected text passes
// QApplication::startDragDistance(), and its release clears Dragging too.
// AdminLog::InnerWidget queues a performDrag() too, but its body past the
// Dragging check is commented out, so it starts nothing.
//
// Shape. AppendQueuedStartDrag appends ONE stage. Its |run| resolves the
// target and its two points (so a widget that appears only after earlier
// stages can be used), delivers the press and the moves through
// Test::DragPressAndMoves, then posts two markers (PostQueuedWorkMarkers
// below): an InvokeQueued to the target and a crl::on_main guarded by the
// target. Its |until| is pure: the target is gone, both markers ran, or
// |bound| ms passed since they were posted. Its |then| delivers the
// release and the leave through Test::DragRelease and Notes one
// QUEUED_DRAG row. A skipped stage (the caller's |skipReason|, passed
// through unchanged) leaves the reading finished=0. The reading is frozen
// in |then|: the markers write their own flags, so a marker that runs late
// cannot change a reading a later stage already judged. The helper never
// Passes or Fails, and owns no timer, no session subscription and no
// Runner::onFinish release - only a QPointer to the target and shared
// pointers.
//
// The guarantee. Qt delivers posted events of one priority in the order
// they were posted, and a delivery pass takes only the events present when
// it begins; crl's main queue is FIFO as well (a GCD main-queue block on
// macOS, a crl queue woken by a posted Ui::MainQueueProcessor event
// elsewhere). The two queues are not ordered against each other, so there
// is one marker per queue and the helper waits for both. When both ran,
// every normal-priority Qt queued call (InvokeQueued, queued signals) and
// every crl::on_main call posted on the main thread while the press and the
// moves - and the postponed calls drained after each of them - were handled
// has run before the release. NOT covered: calls those calls post in turn,
// base::Timer callbacks, network answers, deferred deletes, low-priority
// posted events and calls posted from other threads. Such an effect still
// needs a condition wait of its own.
//
// Refusals, on the returned reading:
// - no-target: the resolver returned no widget; nothing was sent;
// - target-destroyed: the target died during the press or a move, or while
//   its queued calls ran; no release was sent (there is nobody to send it
//   to);
// - bound-expired: the target is alive but a marker had not run |bound| ms
//   after both were posted. The release and the leave ARE still sent: a
//   list left in MouseAction::Dragging would start the drag whenever its
//   queued call finally runs, and a target left pressed would carry the
//   button into every later stage. The reading then says the order was not
//   established.
// The stage's timeout is |bound| plus kDefaultStageTimeout, so the helper's
// own refusal always comes before a runner timeout.
//
// SAFETY - the consumer's obligation. The helper intercepts nothing: an
// interception would be a production seam. Every queued start that reaches
// QDrag::exec() - today through Window::MainWindow::launchDrag() or
// Ui::FlatLabel::executeDrag() - starts a real platform drag. As read in
// Qt 6.11.2's sources: on Windows QWindowsDrag::drag() runs
// OLE's modal DoDragDrop, and QWindowsOleDropSource::QueryContinueDrag
// decides from the REAL mouse buttons
// (QWindowsPointerHandler::queryMouseButtons()), not from the synthetic
// release: it records the first real buttons it sees and drops when they
// change, so with no real button down the loop keeps going until the
// release of the next real click (Escape cancels) and then drops the data
// at the REAL cursor, into whatever window is there. Qt 5 Windows builds
// were not read; assume the same hazard. On macOS QCocoaDrag::drag()
// returns at once only while no real mouse event has reached the app
// (m_lastView still null - QCocoaDrag::setLastInputEvent runs from
// QNSView's handleMouseEvent: for every real press, drag and release, and
// for real moves while the app is active), and otherwise starts a real
// AppKit drag from the last real mouse event, which follows the real
// pointer. Inside QDrag::exec() the event loop keeps turning, so the runner
// can tick there (Runner::tick has no re-entrancy guard), and this
// helper's release and every later stage would run inside that loop. So
// the obligation covers every queued start that reaches QDrag::exec(): the
// consumer hooks the function that creates the QDrag in its overlay, as
// that function's first statement, and returns before new QDrag. The two
// such functions today:
// - Window::MainWindow::launchDrag(), reached from HistoryInner, from
//   HistoryView::ListWidget through WindowListDelegate::listLaunchDrag(),
//   from Info::Media::ListWidget and from Dialogs::InnerWidget. Its hook
//   records the mime data, runs what follows QDrag::exec() -
//   ClickHandler::unpressed() and the callback
//   (Dialogs::InnerWidget::performDrag()'s callback also undoes
//   Ui::Animations::Manager::SetScheduleWithInvokeQueued(true)) - and
//   returns before new QDrag, as the source run's FwdCopyCaptureLaunchDrag
//   hook did.
// - Ui::FlatLabel::executeDrag() (lib_ui), which builds its own QDrag from
//   the selected text or the pressed link's dragText(). Its hook is an
//   overlay hunk in lib_ui that records the mime data, runs
//   ClickHandler::unpressed() and returns before new QDrag. Without that
//   hook, do not drive a Ui::FlatLabel drag with this helper.
// A launch without that hook is not safe to run.
inline constexpr auto kQueuedStartDragBound = crl::time(3000);

// What the resolver answers when the stage begins: the widget that takes
// the press, and the press and release points in its coordinates. A null
// |widget| is refused no-target.
struct QueuedStartDragTarget {
	QWidget *widget = nullptr;
	QPoint from;
	QPoint to;
};

// |name| labels the stage ("queued-start drag: <name>") and the row.
// |steps| and |modifiers| are Test::Drag's. |bound| is how long the stage
// waits for the two markers.
struct QueuedStartDragArgs {
	QString name;
	Fn<QueuedStartDragTarget()> resolve;
	Fn<QString()> skipReason;
	int steps = 8;
	Qt::KeyboardModifiers modifiers = Qt::NoModifier;
	crl::time bound = kQueuedStartDragBound;
};

enum class QueuedStartDragRefusal {
	None,
	NoTarget,
	TargetDestroyed,
	BoundExpired,
};

// The reading the stage fills. |name| and |bound| are filled when the stage
// is appended, so a timeout's details print them; the rest when it runs.
// |movesDelivered| means the press and every move reached a live target;
// |qtQueueRan| and |mainQueueRan| are the two markers as |then| found them;
// |released| means the release and the leave were sent; |waitedMs| counts
// from the markers' posting to |then|. |finished| stays false for a skipped
// stage.
struct QueuedStartDrag {
	QString name;
	QString target;
	QString reason;
	QPoint from;
	QPoint to;
	crl::time bound = 0;
	crl::time waitedMs = 0;
	int steps = 0;
	int startDragDistance = 0;
	QueuedStartDragRefusal refusal = QueuedStartDragRefusal::None;
	bool movesDelivered = false;
	bool qtQueueRan = false;
	bool mainQueueRan = false;
	bool released = false;
	bool finished = false;

	[[nodiscard]] bool delivered() const {
		return finished && (refusal == QueuedStartDragRefusal::None);
	}
};

// none, no-target, target-destroyed, bound-expired.
[[nodiscard]] QString QueuedStartDragRefusalName(
	QueuedStartDragRefusal refusal);

// The row the stage Notes, in ordinary integers:
// "QUEUED_DRAG: name=<name> finished=<0|1> target=<description|none>
// from=<x>,<y> to=<x>,<y> distance=<manhattan length of to - from>
// startDragDistance=<n> steps=<n> bound=<ms> waitedMs=<ms> moves=<0|1>
// qtQueue=<0|1> mainQueue=<0|1> released=<0|1> refusal=<name>", and
// " - <reason>" when the reading carries one.
[[nodiscard]] QString QueuedStartDragText(const QueuedStartDrag &reading);

// Appends the stage "queued-start drag: <name>" and returns the reading it
// fills. Read it from a later stage, and treat a refusal as a fixture gate.
[[nodiscard]] std::shared_ptr<const QueuedStartDrag> AppendQueuedStartDrag(
	not_null<Runner*> runner,
	QueuedStartDragArgs args);

// The two markers AppendQueuedStartDrag waits for, shared with
// Test::AppendQpaMouseDrag (test_qpa_mouse.h). |postedAt| is crl::now()
// when they were posted; each marker sets only its own flag here when it
// runs. Posted after an input's delivery and drain, ran() means what "The
// guarantee" above states, with the exclusions it names.
struct QueuedWorkMarkers {
	crl::time postedAt = 0;
	bool qtQueueRan = false;
	bool mainQueueRan = false;

	[[nodiscard]] bool ran() const {
		return qtQueueRan && mainQueueRan;
	}
};

// Posts one InvokeQueued(context, ...) and one crl::on_main(context, ...),
// in that order, each behind everything already in its own FIFO queue.
// The context's death drops both unrun. The markers hold only the returned
// object, so one that runs late cannot change a reading a stage already
// froze.
[[nodiscard]] std::shared_ptr<const QueuedWorkMarkers> PostQueuedWorkMarkers(
	not_null<QObject*> context);

// The helper measuring itself, over purpose-built probe widgets in a
// never-shown container on the primary window (so the real cursor never
// enters them). A probe records every input it receives; once a move is
// past QApplication::startDragDistance() it posts one InvokeQueued and one
// crl::on_main call that each record whether the release has arrived. The
// stages, in order:
// - fixture: the fixture gate; every judging stage returns early without it;
// - subject: the helper drives a probe; the probe posted from an
//   intermediate move, both of its calls ran before the release, and the
//   drag ends with exactly one release and then one leave;
// - control: Test::Drag drives an identical probe; neither call had run
//   when Drag returned, both ran later and saw the release, so the two legs
//   read opposite orders; Test::Drag delivers exactly its documented
//   sequence (a literal list built from the points and eight steps), and
//   the helper's press, moves, release and leave equal Test::Drag's;
// - destroyed target: a probe deleted by a queued call it posted from its
//   move handler is refused target-destroyed with no release, and the
//   helper's Qt marker never ran. The crl marker is printed but not judged:
//   crl's main queue is not ordered against Qt's posted events;
// - absent target: a resolver answering no widget is refused no-target and
//   nothing is delivered;
// - expired bound: a probe that swallows every QEvent::MetaCall aimed at it
//   never runs the Qt marker and is refused bound-expired after a short
//   bound, while its crl marker ran, and it still gets its release and
//   leave;
// - teardown: the fixture is released (and again, idempotently, through
//   Runner::onFinish).
// No oracle depends on an order across the two queues. It needs no
// session, network or account, emits no deliberate failure, owns no timer
// and starts no QDrag.
void AppendQueuedStartDragSelfTest(not_null<Runner*> runner);

} // namespace Test
