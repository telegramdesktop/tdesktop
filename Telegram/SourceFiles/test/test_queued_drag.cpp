/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_queued_drag.h"

#include "base/invoke_queued.h"
#include "base/unique_qptr.h"
#include "core/application.h"
#include "test/test_capture.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "test/test_widgets.h"
#include "ui/qt_object_factory.h"
#include "ui/rp_widget.h"
#include "window/window_controller.h"

#include <QtCore/QPointer>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QApplication>

namespace Test {
namespace {

// The self-test's bound-expiry leg waits this long for a marker its probe
// swallows; the control leg waits at most kControlWait for the probe's own
// queued calls after Test::Drag returned. kSelfTestSteps is Test::Drag's
// default, which both legs use.
constexpr auto kSelfTestBound = crl::time(300);
constexpr auto kControlWait = crl::time(2000);
constexpr auto kSelfTestSteps = 8;

// Everything one appended stage owns. |markers| is set once they are
// posted; a marker that runs after the stage finished writes only that
// object, never the reading.
struct QueuedStartDragDriver {
	QueuedStartDragArgs args;
	std::shared_ptr<QueuedStartDrag> reading;
	std::shared_ptr<const QueuedWorkMarkers> markers;
	QPointer<QWidget> target;
};

// Record: the subject and the control. DestroyWhileWaiting: once past the
// drag distance it queues its own deletion on its parent, ahead of the
// helper's Qt marker. SwallowQueuedCalls: every QEvent::MetaCall aimed at
// it is withheld, so the helper's Qt marker never runs.
enum class ProbeMode {
	Record,
	DestroyWhileWaiting,
	SwallowQueuedCalls,
};

// |inputs| holds one "<kind> <x>,<y> button=<int> buttons=<int>
// modifiers=<int>" record per mouse event and "leave" per leave; |order|
// holds every kind, including "queued" and "main-queue" when the probe's
// own calls ran. The two optionals say whether each call saw the release.
struct ProbeLog {
	QStringList inputs;
	QStringList order;
	std::optional<bool> queuedSawRelease;
	std::optional<bool> mainQueueSawRelease;
	int crossedAtMove = 0;
	int moves = 0;
	int releases = 0;
	int leaves = 0;
	int swallowed = 0;
	bool released = false;
};

class DragProbe final : public Ui::RpWidget {
public:
	DragProbe(
		QWidget *parent,
		ProbeMode mode,
		std::shared_ptr<ProbeLog> log);

protected:
	bool eventHook(QEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	void record(const QString &kind, QMouseEvent *e);
	void postQueuedCalls();

	const ProbeMode _mode = ProbeMode::Record;
	const std::shared_ptr<ProbeLog> _log;
	QPoint _pressed;
	bool _crossed = false;

};

// Holds only the fixture's owner, QPointers, PODs and shared_ptrs of plain
// structs, so nothing outlives the teardown but data.
struct State {
	base::unique_qptr<Ui::RpWidget> container;
	QPointer<DragProbe> subject;
	QPointer<DragProbe> control;
	QPointer<DragProbe> dies;
	QPointer<DragProbe> swallows;
	std::shared_ptr<ProbeLog> subjectLog;
	std::shared_ptr<ProbeLog> controlLog;
	std::shared_ptr<ProbeLog> diesLog;
	std::shared_ptr<ProbeLog> swallowsLog;
	std::shared_ptr<const QueuedStartDrag> subjectDrag;
	std::shared_ptr<const QueuedStartDrag> diesDrag;
	std::shared_ptr<const QueuedStartDrag> absentDrag;
	std::shared_ptr<const QueuedStartDrag> swallowedDrag;
	QPoint from;
	QPoint to;
	crl::time controlStarted = 0;
	bool controlRanBeforeReturn = false;
	bool built = false;
};

DragProbe::DragProbe(
	QWidget *parent,
	ProbeMode mode,
	std::shared_ptr<ProbeLog> log)
: RpWidget(parent)
, _mode(mode)
, _log(std::move(log)) {
}

bool DragProbe::eventHook(QEvent *e) {
	if (_mode == ProbeMode::SwallowQueuedCalls
		&& e->type() == QEvent::MetaCall) {
		++_log->swallowed;
		return true;
	}
	return RpWidget::eventHook(e);
}

void DragProbe::mousePressEvent(QMouseEvent *e) {
	_pressed = e->position().toPoint();
	record(u"press"_q, e);
	e->accept();
}

void DragProbe::mouseMoveEvent(QMouseEvent *e) {
	++_log->moves;
	record(u"move"_q, e);
	const auto point = e->position().toPoint();
	if (!_crossed
		&& (e->buttons() & Qt::LeftButton)
		&& ((point - _pressed).manhattanLength()
			>= QApplication::startDragDistance())) {
		_crossed = true;
		_log->crossedAtMove = _log->moves;
		postQueuedCalls();
	}
	e->accept();
}

void DragProbe::mouseReleaseEvent(QMouseEvent *e) {
	_log->released = true;
	++_log->releases;
	record(u"release"_q, e);
	e->accept();
}

void DragProbe::leaveEventHook(QEvent *e) {
	++_log->leaves;
	_log->inputs.push_back(u"leave"_q);
	_log->order.push_back(u"leave"_q);
	RpWidget::leaveEventHook(e);
}

void DragProbe::record(const QString &kind, QMouseEvent *e) {
	const auto point = e->position().toPoint();
	_log->inputs.push_back(
		u"%1 %2,%3 button=%4 buttons=%5 modifiers=%6"_q.arg(
			kind,
			QString::number(point.x()),
			QString::number(point.y()),
			QString::number(int(e->button())),
			QString::number(e->buttons().toInt()),
			QString::number(e->modifiers().toInt())));
	_log->order.push_back(kind);
}

void DragProbe::postQueuedCalls() {
	switch (_mode) {
	case ProbeMode::Record: {
		const auto log = _log;
		InvokeQueued(this, [log] {
			log->queuedSawRelease = log->released;
			log->order.push_back(u"queued"_q);
		});
		crl::on_main(this, [log] {
			log->mainQueueSawRelease = log->released;
			log->order.push_back(u"main-queue"_q);
		});
	} break;
	case ProbeMode::DestroyWhileWaiting: {
		InvokeQueued(parentWidget(), [weak = QPointer<QWidget>(this)] {
			delete weak.data();
		});
	} break;
	case ProbeMode::SwallowQueuedCalls: break;
	}
}

[[nodiscard]] QString PointText(QPoint point) {
	return u"%1,%2"_q.arg(point.x()).arg(point.y());
}

[[nodiscard]] QString FlagText(bool value) {
	return value ? u"1"_q : u"0"_q;
}

void StartQueuedStartDrag(QueuedStartDragDriver &driver) {
	const auto &args = driver.args;
	auto &reading = *driver.reading;
	const auto resolved = args.resolve
		? args.resolve()
		: QueuedStartDragTarget();
	reading.from = resolved.from;
	reading.to = resolved.to;
	reading.steps = std::max(args.steps, 1);
	reading.startDragDistance = QApplication::startDragDistance();
	const auto widget = resolved.widget;
	if (!widget) {
		reading.refusal = QueuedStartDragRefusal::NoTarget;
		reading.reason = u"the resolver returned no widget; nothing was "
			u"sent"_q;
		return;
	}
	driver.target = widget;
	reading.target = WidgetDescription(widget);
	if (!DragPressAndMoves(
			widget,
			reading.from,
			reading.to,
			reading.steps,
			args.modifiers)) {
		reading.refusal = QueuedStartDragRefusal::TargetDestroyed;
		reading.reason = u"the target was destroyed while the press and the "
			u"moves were handled; no release was sent"_q;
		return;
	}
	reading.movesDelivered = true;

	// Posted after every call the press and the moves queued, so each runs
	// after them in its own FIFO queue. The Qt marker's context is the
	// target, so the target's death drops it together with the target's own
	// queued calls; the crl marker is guarded by the target.
	driver.markers = PostQueuedWorkMarkers(widget);
}

[[nodiscard]] bool QueuedStartDragSettled(
		const QueuedStartDragDriver &driver) {
	const auto markers = driver.markers.get();
	return !markers
		|| !driver.target
		|| markers->ran()
		|| (crl::now() - markers->postedAt >= driver.args.bound);
}

void FinishQueuedStartDrag(QueuedStartDragDriver &driver) {
	auto &reading = *driver.reading;
	if (const auto markers = driver.markers.get()) {
		reading.qtQueueRan = markers->qtQueueRan;
		reading.mainQueueRan = markers->mainQueueRan;
		reading.waitedMs = crl::now() - markers->postedAt;
		const auto queues = u"qtQueue=%1 mainQueue=%2"_q.arg(
			FlagText(reading.qtQueueRan),
			FlagText(reading.mainQueueRan));
		if (const auto target = driver.target.data()) {
			if (!reading.qtQueueRan || !reading.mainQueueRan) {
				reading.refusal = QueuedStartDragRefusal::BoundExpired;
				reading.reason = u"the markers had not both run %1 ms after "
					u"they were posted (%2); the release and the leave were "
					u"sent anyway, so the order is not established"_q.arg(
						QString::number(driver.args.bound),
						queues);
			}
			DragRelease(target, reading.to, driver.args.modifiers);
			reading.released = true;
		} else {
			reading.refusal = QueuedStartDragRefusal::TargetDestroyed;
			reading.reason = u"the target was destroyed while its queued "
				u"calls ran (%1); no release was sent"_q.arg(queues);
		}
	}
	reading.finished = true;
	Note(QueuedStartDragText(reading));
}

[[nodiscard]] QString SawReleaseText(const std::optional<bool> &value) {
	return !value ? u"unset"_q : FlagText(*value);
}

[[nodiscard]] QString ProbeLogText(
		const QString &label,
		const ProbeLog &log) {
	return u"%1: crossedAtMove=%2 moves=%3 releases=%4 leaves=%5 "
		u"swallowed=%6 queuedSawRelease=%7 mainQueueSawRelease=%8 "
		u"order=[%9]"_q.arg(
			label,
			QString::number(log.crossedAtMove),
			QString::number(log.moves),
			QString::number(log.releases),
			QString::number(log.leaves),
			QString::number(log.swallowed),
			SawReleaseText(log.queuedSawRelease),
			SawReleaseText(log.mainQueueSawRelease),
			log.order.join(u", "_q));
}

[[nodiscard]] bool SawRelease(
		const std::optional<bool> &value,
		bool expected) {
	return value.has_value() && (*value == expected);
}

// The probe's own |kind| call ran, saw no release, and sits before the
// release in the probe's order.
[[nodiscard]] bool RanBeforeRelease(
		const ProbeLog &log,
		const std::optional<bool> &sawRelease,
		const QString &kind) {
	const auto call = log.order.indexOf(kind);
	const auto release = log.order.indexOf(u"release"_q);
	return SawRelease(sawRelease, false)
		&& (call >= 0)
		&& (release >= 0)
		&& (call < release);
}

// Test::Drag's contract as its comment and body state it, independent of
// the code that delivers it: a press at |from|, |steps| interpolated moves
// that keep Qt::LeftButton in buttons(), a release at |to| with no buttons
// left, then the leave.
[[nodiscard]] QStringList DocumentedDragSequence(
		QPoint from,
		QPoint to,
		int steps) {
	auto result = QStringList();
	result.push_back(u"press %1 button=1 buttons=1 modifiers=0"_q.arg(
		PointText(from)));
	for (auto step = 1; step <= steps; ++step) {
		const auto point = from + ((to - from) * step) / steps;
		result.push_back(u"move %1 button=0 buttons=1 modifiers=0"_q.arg(
			PointText(point)));
	}
	result.push_back(u"release %1 button=1 buttons=0 modifiers=0"_q.arg(
		PointText(to)));
	result.push_back(u"leave"_q);
	return result;
}

[[nodiscard]] bool BuildFixture(State &state) {
	const auto window = Core::App().activePrimaryWindow();
	if (!window) {
		return false;
	}
	const auto d = std::max(QApplication::startDragDistance(), 1);
	state.container = base::make_unique_q<Ui::RpWidget>(
		window->widget().get());
	const auto container = state.container.get();

	// Never shown, as test_hover's fixture: the real cursor can never enter
	// a hidden container, so only the synthetic input reaches the probes.
	container->resize(6 * d, 8 * d);
	auto index = 0;
	const auto add = [&](ProbeMode mode, std::shared_ptr<ProbeLog> &log) {
		log = std::make_shared<ProbeLog>();
		const auto probe = Ui::CreateChild<DragProbe>(container, mode, log);
		probe->setGeometry(0, 2 * d * index++, 6 * d, 2 * d);
		probe->show();
		return QPointer<DragProbe>(probe);
	};
	state.subject = add(ProbeMode::Record, state.subjectLog);
	state.control = add(ProbeMode::Record, state.controlLog);
	state.dies = add(ProbeMode::DestroyWhileWaiting, state.diesLog);
	state.swallows = add(ProbeMode::SwallowQueuedCalls, state.swallowsLog);

	// Four drag distances in eight steps: the crossing posts from the second
	// move, an intermediate one, as a real drag-out does.
	state.from = QPoint(d, d);
	state.to = state.from + QPoint(4 * d, 0);
	return true;
}

void ReleaseFixture(State &state) {
	state.container = nullptr;
	state.subject = nullptr;
	state.control = nullptr;
	state.dies = nullptr;
	state.swallows = nullptr;
}

void CheckSubjectReading(const State &state) {
	if (!state.built) {
		return;
	}
	const auto &log = *state.subjectLog;
	const auto &drag = *state.subjectDrag;
	const auto logText = ProbeLogText(u"subject"_q, log);
	Check(
		(log.crossedAtMove >= 1) && (log.crossedAtMove < kSelfTestSteps),
		u"fixture gate: the probe posted its queued calls from an "
		u"intermediate move"_q,
		u"%1 steps=%2 startDragDistance=%3"_q.arg(
			logText,
			QString::number(kSelfTestSteps),
			QString::number(QApplication::startDragDistance())));
	Check(
		drag.delivered()
			&& drag.qtQueueRan
			&& drag.mainQueueRan
			&& drag.released,
		u"queued drag self-test: the helper delivered the drag with both "
		u"markers run before the release"_q,
		QueuedStartDragText(drag));
	Check(
		RanBeforeRelease(log, log.queuedSawRelease, u"queued"_q),
		u"queued drag self-test: under the helper the InvokeQueued call "
		u"posted from the move handler ran before the release"_q,
		logText);
	Check(
		RanBeforeRelease(log, log.mainQueueSawRelease, u"main-queue"_q),
		u"queued drag self-test: under the helper the crl::on_main call "
		u"posted from the move handler ran before the release"_q,
		logText);
	const auto count = log.order.size();
	Check(
		(log.releases == 1)
			&& (log.leaves == 1)
			&& (count >= 2)
			&& (log.order[count - 2] == u"release"_q)
			&& (log.order[count - 1] == u"leave"_q),
		u"queued drag self-test: the helper ends with exactly one release "
		u"and then one leave"_q,
		logText);
}

void StartControlDrag(State &state) {
	const auto control = state.control.data();
	if (!state.built || !control) {
		return;
	}
	state.controlStarted = crl::now();
	Drag(control, state.from, state.to);
	const auto &log = *state.controlLog;
	state.controlRanBeforeReturn = log.queuedSawRelease.has_value()
		|| log.mainQueueSawRelease.has_value();
}

[[nodiscard]] bool ControlSettled(const State &state) {
	if (!state.built) {
		return true;
	}
	const auto &log = *state.controlLog;
	return (log.queuedSawRelease.has_value()
			&& log.mainQueueSawRelease.has_value())
		|| (crl::now() - state.controlStarted > kControlWait);
}

void CheckControlReading(const State &state) {
	if (!state.built) {
		return;
	}
	const auto &subject = *state.subjectLog;
	const auto &control = *state.controlLog;
	const auto controlText = ProbeLogText(u"control"_q, control);
	Check(
		!state.controlRanBeforeReturn,
		u"queued drag self-test: under Test::Drag (control) neither call "
		u"had run when Drag returned"_q,
		controlText);
	Check(
		SawRelease(control.queuedSawRelease, true)
			&& SawRelease(control.mainQueueSawRelease, true),
		u"queued drag self-test: under Test::Drag (control) the release "
		u"arrived first: both calls ran later and saw it"_q,
		u"%1 waitedMs=%2"_q.arg(
			controlText,
			QString::number(crl::now() - state.controlStarted)));
	Check(
		SawRelease(subject.queuedSawRelease, false)
			&& SawRelease(subject.mainQueueSawRelease, false)
			&& SawRelease(control.queuedSawRelease, true)
			&& SawRelease(control.mainQueueSawRelease, true),
		u"queued drag self-test: the two legs read opposite orders, so the "
		u"self-test tells them apart"_q,
		u"helper: queuedSawRelease=%1 mainQueueSawRelease=%2; Test::Drag: "
		u"queuedSawRelease=%3 mainQueueSawRelease=%4"_q.arg(
			SawReleaseText(subject.queuedSawRelease),
			SawReleaseText(subject.mainQueueSawRelease),
			SawReleaseText(control.queuedSawRelease),
			SawReleaseText(control.mainQueueSawRelease)));
	const auto documented = DocumentedDragSequence(
		state.from,
		state.to,
		kSelfTestSteps);
	Check(
		control.inputs == documented,
		u"queued drag self-test: Test::Drag delivers exactly its documented "
		u"sequence"_q,
		u"documented=[%1] delivered=[%2]"_q.arg(
			documented.join(u"; "_q),
			control.inputs.join(u"; "_q)));
	Check(
		subject.inputs == control.inputs,
		u"queued drag self-test: the helper's press, moves, release and "
		u"leave equal Test::Drag's"_q,
		u"helper=[%1] Test::Drag=[%2]"_q.arg(
			subject.inputs.join(u"; "_q),
			control.inputs.join(u"; "_q)));
}

void CheckRefusedTargets(const State &state) {
	if (!state.built) {
		return;
	}
	const auto &dies = *state.diesDrag;
	const auto &diesLog = *state.diesLog;
	const auto diesText = QueuedStartDragText(dies);
	Check(
		dies.finished
			&& (dies.refusal == QueuedStartDragRefusal::TargetDestroyed)
			&& dies.movesDelivered
			&& !dies.released
			&& !dies.qtQueueRan
			&& (diesLog.releases == 0)
			&& !state.dies
			&& diesText.contains(u"refusal=target-destroyed"_q),
		u"queued drag self-test: a target destroyed by a queued call while "
		u"the helper waits is refused target-destroyed with no release"_q,
		u"%1; %2 probeAlive=%3; mainQueue=%4 is not judged: crl's main "
		u"queue is not ordered against Qt's posted events"_q.arg(
			diesText,
			ProbeLogText(u"destroyed probe"_q, diesLog),
			FlagText(!state.dies.isNull()),
			FlagText(dies.mainQueueRan)));
	const auto &absent = *state.absentDrag;
	Check(
		absent.finished
			&& (absent.refusal == QueuedStartDragRefusal::NoTarget)
			&& !absent.movesDelivered
			&& !absent.released
			&& !absent.reason.isEmpty(),
		u"queued drag self-test: an absent target is refused no-target and "
		u"nothing is delivered"_q,
		QueuedStartDragText(absent));
}

void CheckExpiredBound(const State &state) {
	if (!state.built) {
		return;
	}
	const auto &drag = *state.swallowedDrag;
	const auto &log = *state.swallowsLog;
	const auto text = QueuedStartDragText(drag);
	Check(
		(drag.refusal == QueuedStartDragRefusal::BoundExpired)
			&& !drag.qtQueueRan
			&& drag.mainQueueRan
			&& drag.released
			&& (drag.waitedMs >= kSelfTestBound)
			&& (log.swallowed >= 1)
			&& (log.releases == 1)
			&& (log.leaves == 1)
			&& text.contains(u"refusal=bound-expired"_q)
			&& text.contains(u"qtQueue=0"_q),
		u"queued drag self-test: a target that never runs the helper's "
		u"queued marker is refused bound-expired after the bound and still "
		u"gets its release and leave"_q,
		u"%1; %2"_q.arg(text, ProbeLogText(u"swallowing probe"_q, log)));
}

} // namespace

QString QueuedStartDragRefusalName(QueuedStartDragRefusal refusal) {
	switch (refusal) {
	case QueuedStartDragRefusal::None: return u"none"_q;
	case QueuedStartDragRefusal::NoTarget: return u"no-target"_q;
	case QueuedStartDragRefusal::TargetDestroyed:
		return u"target-destroyed"_q;
	case QueuedStartDragRefusal::BoundExpired: return u"bound-expired"_q;
	}
	Unexpected("Refusal in Test::QueuedStartDragRefusalName.");
}

QString QueuedStartDragText(const QueuedStartDrag &reading) {
	const auto line = u"QUEUED_DRAG: name=%1 finished=%2 target=%3 from=%4 "
		u"to=%5 distance=%6 startDragDistance=%7 steps=%8 bound=%9 "
		u"waitedMs=%10 moves=%11 qtQueue=%12 mainQueue=%13 released=%14 "
		u"refusal=%15"_q.arg(
			reading.name,
			FlagText(reading.finished),
			reading.target.isEmpty() ? u"none"_q : reading.target,
			PointText(reading.from),
			PointText(reading.to),
			QString::number((reading.to - reading.from).manhattanLength()),
			QString::number(reading.startDragDistance),
			QString::number(reading.steps),
			QString::number(reading.bound),
			QString::number(reading.waitedMs),
			FlagText(reading.movesDelivered),
			FlagText(reading.qtQueueRan),
			FlagText(reading.mainQueueRan),
			FlagText(reading.released),
			QueuedStartDragRefusalName(reading.refusal));
	return reading.reason.isEmpty()
		? line
		: (line + u" - "_q + reading.reason);
}

std::shared_ptr<const QueuedStartDrag> AppendQueuedStartDrag(
		not_null<Runner*> runner,
		QueuedStartDragArgs args) {
	const auto driver = std::make_shared<QueuedStartDragDriver>();
	driver->args = std::move(args);
	driver->reading = std::make_shared<QueuedStartDrag>();
	driver->reading->name = driver->args.name;
	driver->reading->bound = driver->args.bound;
	runner->add({
		.name = u"queued-start drag: %1"_q.arg(driver->args.name),
		.skipReason = driver->args.skipReason,
		.run = [=] {
			StartQueuedStartDrag(*driver);
		},
		.until = [=] {
			return QueuedStartDragSettled(*driver);
		},
		.then = [=] {
			FinishQueuedStartDrag(*driver);
		},
		.timeout = std::max(driver->args.bound, crl::time())
			+ kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return QueuedStartDragText(*driver->reading);
		},
	});
	return driver->reading;
}

std::shared_ptr<const QueuedWorkMarkers> PostQueuedWorkMarkers(
		not_null<QObject*> context) {
	const auto result = std::make_shared<QueuedWorkMarkers>();
	result->postedAt = crl::now();
	InvokeQueued(context.get(), [result] {
		result->qtQueueRan = true;
	});
	crl::on_main(context.get(), [result] {
		result->mainQueueRan = true;
	});
	return result;
}

void AppendQueuedStartDragSelfTest(not_null<Runner*> runner) {
	const auto state = std::make_shared<State>();
	runner->onFinish([=] {
		state->container = nullptr;
	});

	runner->add({
		.name = u"queued drag self-test: fixture"_q,
		.run = [=] {
			state->built = BuildFixture(*state);
			Check(
				state->built,
				u"fixture gate: the queued drag self-test fixture was "
				u"built"_q,
				state->built
					? QString()
					: u"Core::App().activePrimaryWindow() is null"_q);
		},
	});

	state->subjectDrag = AppendQueuedStartDrag(runner, {
		.name = u"self-test subject"_q,
		.resolve = [=] {
			return QueuedStartDragTarget{
				state->subject.data(),
				state->from,
				state->to,
			};
		},
	});

	runner->add({
		.name = u"queued drag self-test: subject reading"_q,
		.run = [=] {
			CheckSubjectReading(*state);
		},
	});

	runner->add({
		.name = u"queued drag self-test: Test::Drag control"_q,
		.run = [=] {
			StartControlDrag(*state);
		},
		.until = [=] {
			return ControlSettled(*state);
		},
		.then = [=] {
			CheckControlReading(*state);
		},
	});

	state->diesDrag = AppendQueuedStartDrag(runner, {
		.name = u"self-test destroyed target"_q,
		.resolve = [=] {
			return QueuedStartDragTarget{
				state->dies.data(),
				state->from,
				state->to,
			};
		},
	});

	state->absentDrag = AppendQueuedStartDrag(runner, {
		.name = u"self-test no target"_q,
		.resolve = [] {
			return QueuedStartDragTarget();
		},
	});

	runner->add({
		.name = u"queued drag self-test: destroyed and absent targets are "
			u"refused"_q,
		.run = [=] {
			CheckRefusedTargets(*state);
		},
	});

	state->swallowedDrag = AppendQueuedStartDrag(runner, {
		.name = u"self-test swallowed marker"_q,
		.resolve = [=] {
			return QueuedStartDragTarget{
				state->swallows.data(),
				state->from,
				state->to,
			};
		},
		.bound = kSelfTestBound,
	});

	runner->add({
		.name = u"queued drag self-test: an expired bound is refused"_q,
		.run = [=] {
			CheckExpiredBound(*state);
		},
	});

	runner->add({
		.name = u"queued drag self-test: teardown"_q,
		.run = [=] {
			ReleaseFixture(*state);
			Note(u"queued drag self-test: fixture released, alive=%1"_q.arg(
				FlagText(state->container.get() != nullptr)));
		},
	});
}

} // namespace Test

#endif // _DEBUG
