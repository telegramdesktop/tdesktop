/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_qpa_mouse.h"

#include "base/algorithm.h"
#include "base/event_filter.h"
#include "base/invoke_queued.h"
#include "base/unique_qptr.h"
#include "test/test_capture.h"
#include "test/test_console_shield.h"
#include "test/test_log.h"
#include "test/test_queued_drag.h"
#include "test/test_runner.h"
#include "test/test_widgets.h"
#include "ui/effects/animation_value.h"
#include "ui/effects/animations.h"
#include "ui/qt_object_factory.h"
#include "ui/rp_widget.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QEvent>
#include <QtCore/QLocale>
#include <QtGui/QCursor>
#include <QtGui/QGuiApplication>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QScreen>
#include <QtGui/QStyleHints>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
#include <private/qguiapplication_p.h>
#include <private/qhighdpiscaling_p.h>
#include <qpa/qwindowsysteminterface.h>
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API

#include "styles/palette.h"
#include "styles/style_widgets.h"

namespace Test {
namespace {

// The probe animation lasts kProbeAnimation from the release that starts
// it; an observation waits at most kAnimationBound from that release, and
// the control at most kControlWait for its pair's own queued calls. A pair
// posts those calls from held move kPostAtMove, an intermediate one of
// kSelfTestSteps. Positions match within kPositionTolerance logical px.
constexpr auto kProbeAnimation = crl::time(200);
constexpr auto kAnimationBound = crl::time(2000);
constexpr auto kControlWait = crl::time(2000);
constexpr auto kSelfTestSteps = 8;
constexpr auto kPostAtMove = 2;
constexpr auto kPositionTolerance = 0.01;

// The paced legs deliver one move per kSelfTestPace ms, as one earlier
// wallet campaign did; the expiry leg waits kExpiryBound for markers that
// cannot both run. An unconverted delivery lands "near" P / factor within
// kNearTolerance logical px, and discriminates when it lands farther than
// that from P.
constexpr auto kSelfTestPace = crl::time(50);
constexpr auto kExpiryBound = crl::time(300);
constexpr auto kNearTolerance = 0.5;

// The label of the console shield arm the self-test owns.
const auto kShieldLabel = u"qpa-mouse-self-test"_q;

#ifdef DESKTOP_APP_DISABLE_PRIVATE_QT_API
constexpr auto kPlatformInput = false;
#else // DESKTOP_APP_DISABLE_PRIVATE_QT_API
constexpr auto kPlatformInput = true;

using Delivery = QWindowSystemInterface::SynchronousDelivery;
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API

// Record: the subject, the control and the armed leg. DeleteAtMove: at
// held move kPostAtMove it queues its own deletion on its window, ahead of
// the drag's next paced move. SwallowQueuedCalls: every QEvent::MetaCall
// aimed at it is withheld, so the drag's Qt marker never runs.
enum class ParentMode {
	Record,
	DeleteAtMove,
	SwallowQueuedCalls,
};

// One mouse, enter or leave event as a probe read it inside its handler,
// or as the self-test expects it. |grabber| is QWidget::mouseGrabber() at
// that moment, as GrabberText names it; |local| and |global| are the
// event's logical positions.
struct ProbeEvent {
	QString who;
	QString grabber;
	QPointF local;
	QPointF global;
	crl::time at = 0;
	quint64 timestamp = 0;
	QEvent::Type type = QEvent::None;
	Qt::MouseButton button = Qt::NoButton;
	Qt::MouseButtons buttons = Qt::NoButton;
	Qt::KeyboardModifiers modifiers = Qt::NoModifier;
	bool spontaneous = false;
};

// What one probe pair, or the landing pad, received during one leg.
// |order| holds "<who>:<kind>" per event, and "queued" and "main-queue"
// when the pair's own two calls ran; the optionals say whether each call
// saw a release reach the pair, |postedBy| names the probe that posted
// them. The rest is the GrabParent's release animation: |releasedAt| is
// its release, |ticks| counts the animation's callbacks. A DeleteAtMove
// parent adds "turn:<n>" when the turn that gave it held move n ended and
// "destroyed" right after its deletion, at |destroyedAt|; a
// SwallowQueuedCalls parent counts what it withheld in |swallowed|.
struct ProbeLog {
	std::vector<ProbeEvent> events;
	QStringList order;
	std::optional<bool> queuedSawRelease;
	std::optional<bool> mainQueueSawRelease;
	QString postedBy;
	crl::time releasedAt = 0;
	crl::time animationStartedAt = 0;
	crl::time animationCompletedAt = 0;
	crl::time destroyedAt = 0;
	int heldMoves = 0;
	int ticks = 0;
	int swallowed = 0;
	bool released = false;
	bool animationCompleted = false;
};

// The mouse events one fixture QWindow received - the first hop of QPA
// input, which a sent event never passes - with their spontaneous flags.
struct WindowEvent {
	QEvent::Type type = QEvent::None;
	bool spontaneous = false;
};

struct WindowLog {
	std::vector<WindowEvent> events;
};

// Accepts every mouse event it gets and forwards only its press and its
// release to its parent: a copy sent with the position mapped there and
// the same global position, as the motivating grid's squares forward
// theirs.
class ForwardingChild final : public Ui::RpWidget {
public:
	explicit ForwardingChild(QWidget *parent);

	void setLog(std::shared_ptr<ProbeLog> log);

protected:
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	void forward(not_null<QMouseEvent*> e);

	std::shared_ptr<ProbeLog> _log;

};

// Grabs the mouse on a left press and keeps every mouse event it gets; on
// a release it gives the grab back and starts a kProbeAnimation-long
// Ui::Animations::Basic, counting its ticks and recording its end. Its
// |mode| adds a refusal leg's behaviour.
class GrabParent final : public Ui::RpWidget {
public:
	GrabParent(QWidget *parent, ParentMode mode);

	// Stops the animation first, so no tick writes into the new log.
	void setLog(std::shared_ptr<ProbeLog> log);
	void stopAnimation();
	[[nodiscard]] bool animating() const;

protected:
	bool eventHook(QEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	[[nodiscard]] bool tick(crl::time now);

	const ParentMode _mode = ParentMode::Record;
	std::shared_ptr<ProbeLog> _log;
	Ui::Animations::Basic _animation;

};

// Accepts and keeps every mouse event, double clicks included, with its
// exact positions and timestamp.
class LandingPad final : public Ui::RpWidget {
public:
	explicit LandingPad(QWidget *parent);

	void setLog(std::shared_ptr<ProbeLog> log);

protected:
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	std::shared_ptr<ProbeLog> _log;

};

struct ProbePair {
	QPointer<GrabParent> parent;
	QPointer<ForwardingChild> child;
	std::shared_ptr<ProbeLog> log;
};

// Qt's pointer state over the fixture: the held buttons, the mouse grabber
// and the fixture's widgets, windows included, with underMouse().
struct EndState {
	QStringList hovered;
	QString grabber;
	Qt::MouseButtons buttons = Qt::NoButton;
	bool read = false;

	[[nodiscard]] bool clean() const {
		return read && !buttons && grabber.isEmpty() && hovered.isEmpty();
	}
};

// A direct QWindowSystemInterface gesture of an ingredient, coordinate or
// double-click leg: its window, its last native points and the buttons it
// still holds, so its cleanup needs neither a QpaMouseSequence nor a live
// probe.
struct RawGesture {
	QPointer<QWindow> window;
	QPointF nativeLocal;
	QPointF nativeGlobal;
	Qt::MouseButtons held = Qt::NoButton;
	bool sent = false;
};

// One direct left-button event for SendDirect, at native points the caller
// chose. |timestamp| zero leaves the stamp to Qt; |keepPairing| skips the
// pre-press reset of Qt's double-click pairing button.
struct DirectEvent {
	QPointF nativeLocal;
	QPointF nativeGlobal;
	ulong timestamp = 0;
	QEvent::Type type = QEvent::None;
	bool settle = false;
	bool keepPairing = false;
};

// One of Run 1's ingredient legs on the subject pair: the helper's points
// and pre-press preparation, through direct QWindowSystemInterface calls,
// each inside Test::Settle only with |settle|, and the release only after
// the queued-work markers ran with |wait|. |route| and |window| are what
// the probes and the window got, read when the observation ended.
struct IngredientLeg {
	QString name;
	QString row;
	QString markersText;
	QString cleanup;
	std::shared_ptr<ProbeLog> log;
	std::shared_ptr<const QueuedWorkMarkers> markers;
	std::shared_ptr<const QueuedWorkMarkers> drain;
	ProbeLog route;
	std::vector<WindowEvent> window;
	EndState start;
	EndState end;
	crl::time releaseSentAt = 0;
	int movesSent = 0;
	bool settle = false;
	bool wait = false;
	bool pressSent = false;
	bool releaseSent = false;
	bool animatingBefore = false;
};

// The coordinate leg on the landing pad. |point| is P in the pads window's
// logical coordinates and |padPoint| the same point in the pad's; |raw|
// and |converted| are what the pad got from the unconverted control and
// from the helper; the end states are read after each one's cleanup.
struct CoordinateLeg {
	ProbeLog raw;
	ProbeLog converted;
	QpaMouseEnd end;
	EndState rawEnd;
	EndState convertedEnd;
	QString rawCleanup;
	QString refusal;
	QPoint point;
	QPoint padPoint;
	QPointF origin;
	double factor = 0.;
	double dpr = 0.;
};

// The double-click legs on the landing pad: two helper clicks a runner
// tick apart, then two direct clicks stamped from |base|, the last
// timestamp the helper's clicks left. |repeated| is the second click's
// end() called once more, with the events that call added.
struct ClickLegs {
	std::shared_ptr<ProbeLog> facility;
	std::shared_ptr<ProbeLog> raw;
	std::vector<WindowEvent> facilityWindow;
	std::vector<WindowEvent> rawWindow;
	QpaMouseEnd firstEnd;
	QpaMouseEnd secondEnd;
	QpaMouseEnd repeated;
	EndState facilityEnd;
	EndState rawEnd;
	QString rawCleanup;
	QPoint point;
	quint64 base = 0;
	int repeatedEvents = 0;
	int repeatedWindowEvents = 0;
};

// The subject gesture again, paced, while the self-test's own console
// shield arm holds; |before| and |after| are the shield around it.
struct ArmedLeg {
	std::shared_ptr<ProbeLog> log;
	std::shared_ptr<const QpaMouseDrag> drag;
	ConsoleShieldReading before;
	ConsoleShieldReading after;
	ConsoleShieldReading released;
	ProbeLog route;
	std::vector<WindowEvent> window;
	EndState end;
	bool releasedOwned = false;
};

// The targets refused before any delivery: a parentless window never
// shown, and one shown with Qt::WA_DontShowOnScreen, each with a recording
// LandingPad the drags aim at.
struct RefusalTargets {
	base::unique_qptr<Ui::RpWidget> unshown;
	base::unique_qptr<Ui::RpWidget> offscreen;
	QPointer<LandingPad> unshownPad;
	QPointer<LandingPad> offscreenPad;
	std::shared_ptr<ProbeLog> unshownLog;
	std::shared_ptr<ProbeLog> offscreenLog;
	std::shared_ptr<const QpaMouseDrag> unshownDrag;
	std::shared_ptr<const QpaMouseDrag> offscreenDrag;
	bool unshownHandleBefore = false;
};

// |pairs| holds the subject and control pairs, |pads| the landing pad; the
// stages and the finish callback share it. |from| and |to| are in a pair
// parent's coordinates. The subject's and control's route readings are
// taken right after their gestures' cleanup. Once the control's legs are
// done its row, at |spareOrigin|, takes |spare|: the pair that deletes
// itself, then the pair that swallows queued calls.
struct State {
	base::unique_qptr<Ui::RpWidget> pairs;
	base::unique_qptr<Ui::RpWidget> pads;
	QPointer<LandingPad> pad;
	std::shared_ptr<ProbeLog> padLog;
	std::shared_ptr<WindowLog> pairsWindow;
	std::shared_ptr<WindowLog> padsWindow;
	ProbePair subject;
	ProbePair control;
	ProbePair spare;
	std::shared_ptr<ProbeLog> subjectLog;
	std::shared_ptr<const QpaMouseDrag> subjectDrag;
	ProbeLog subjectRoute;
	std::vector<WindowEvent> subjectWindow;
	EndState subjectEnd;
	QString subjectRow;
	ProbeLog controlRoute;
	std::vector<WindowEvent> controlWindow;
	EndState controlAfterDrag;
	EndState controlEnd;
	IngredientLeg raw;
	IngredientLeg settleOnly;
	IngredientLeg waitOnly;
	CoordinateLeg coordinates;
	ClickLegs clicks;
	ArmedLeg armed;
	RefusalTargets refused;
	std::shared_ptr<ProbeLog> destroyedLog;
	std::shared_ptr<const QpaMouseDrag> destroyedDrag;
	std::shared_ptr<ProbeLog> expiryLog;
	std::shared_ptr<const QpaMouseDrag> expiryDrag;
	RawGesture gesture;
	QString gate = u"fixture gate: the fixture stage has not run"_q;
	QString buildRefusal;
	QString placement;
	QPoint from;
	QPoint to;
	QPoint spareOrigin;
	int cell = 0;
	crl::time subjectObservedFrom = 0;
	crl::time controlStarted = 0;
	bool controlRanBeforeReturn = false;
	bool fits = false;
};

struct Placement {
	QString text;
	bool fits = false;
};

[[nodiscard]] QString Describe(QWidget *widget) {
	return widget ? WidgetDescription(widget) : QString();
}

[[nodiscard]] QString NumberText(double value) {
	return QString::number(value, 'g', QLocale::FloatingPointShortest);
}

[[nodiscard]] QString PointText(QPointF point) {
	return NumberText(point.x()) + u","_q + NumberText(point.y());
}

[[nodiscard]] QString DescriptionText(const QString &description) {
	return description.isEmpty() ? u"none"_q : description;
}

[[nodiscard]] QString FlagText(bool value) {
	return QString::number(value ? 1 : 0);
}

[[nodiscard]] bool IsMouse(QEvent::Type type) {
	switch (type) {
	case QEvent::MouseButtonPress:
	case QEvent::MouseButtonRelease:
	case QEvent::MouseButtonDblClick:
	case QEvent::MouseMove:
		return true;
	default:
		return false;
	}
}

[[nodiscard]] QString KindName(QEvent::Type type) {
	switch (type) {
	case QEvent::MouseButtonPress: return u"press"_q;
	case QEvent::MouseMove: return u"move"_q;
	case QEvent::MouseButtonRelease: return u"release"_q;
	case QEvent::MouseButtonDblClick: return u"double"_q;
	case QEvent::Enter: return u"enter"_q;
	case QEvent::Leave: return u"leave"_q;
	default: return QString::number(int(type));
	}
}

// QWidget::mouseGrabber() as a probe reads it: "none", "parent" for its
// own pair's GrabParent, or "other".
[[nodiscard]] QString GrabberText(const QWidget *pairParent) {
	const auto grabber = QWidget::mouseGrabber();
	return !grabber
		? u"none"_q
		: (pairParent && (grabber == pairParent))
		? u"parent"_q
		: u"other"_q;
}

void RecordMouse(
		ProbeLog &log,
		const QString &who,
		const QWidget *pairParent,
		not_null<QMouseEvent*> e) {
	log.events.push_back({
		.who = who,
		.grabber = GrabberText(pairParent),
		.local = e->position(),
		.global = e->globalPosition(),
		.at = crl::now(),
		.timestamp = e->timestamp(),
		.type = e->type(),
		.button = e->button(),
		.buttons = e->buttons(),
		.modifiers = e->modifiers(),
		.spontaneous = e->spontaneous(),
	});
	log.order.push_back(who + u":"_q + KindName(e->type()));
	if (e->type() == QEvent::MouseButtonRelease) {
		log.released = true;
	}
}

void RecordHover(
		ProbeLog &log,
		const QString &who,
		const QWidget *pairParent,
		not_null<QEvent*> e) {
	log.events.push_back({
		.who = who,
		.grabber = GrabberText(pairParent),
		.at = crl::now(),
		.type = e->type(),
		.spontaneous = e->spontaneous(),
	});
	log.order.push_back(who + u":"_q + KindName(e->type()));
}

// The pair's own two calls, posted once, from held move kPostAtMove, with
// the probe that got that move as their context. Each reads whether a
// release had reached the pair when it ran.
void CountHeldMove(
		const std::shared_ptr<ProbeLog> &log,
		const QString &who,
		not_null<QObject*> context,
		not_null<QMouseEvent*> e) {
	if (!(e->buttons() & Qt::LeftButton)) {
		return;
	} else if (++log->heldMoves != kPostAtMove) {
		return;
	}
	log->postedBy = who;
	InvokeQueued(context.get(), [log] {
		log->queuedSawRelease = log->released;
		log->order.push_back(u"queued"_q);
	});
	crl::on_main(context.get(), [log] {
		log->mainQueueSawRelease = log->released;
		log->order.push_back(u"main-queue"_q);
	});
}

// A DeleteAtMove parent's held move n: a call queued on its window marks
// the end of the turn that delivered it, so "turn:<n>" lands between two
// moves only when they came in separate turns. At held move kPostAtMove
// its own deletion is queued there first, so it runs before the drag's
// next paced move and "destroyed" follows it.
void QueueOwnDeletion(
		const std::shared_ptr<ProbeLog> &log,
		not_null<QWidget*> parent,
		not_null<QMouseEvent*> e) {
	if (!(e->buttons() & Qt::LeftButton)) {
		return;
	}
	const auto move = ++log->heldMoves;
	const auto top = parent->window();
	if (move == kPostAtMove) {
		InvokeQueued(top, [log, weak = QPointer<QWidget>(parent.get())] {
			delete weak.data();
			log->destroyedAt = crl::now();
			log->order.push_back(u"destroyed"_q);
		});
	}
	InvokeQueued(top, [log, move] {
		log->order.push_back(u"turn:"_q + QString::number(move));
	});
}

ForwardingChild::ForwardingChild(QWidget *parent)
: RpWidget(parent)
, _log(std::make_shared<ProbeLog>()) {
	paintOn([=](QPainter &p) {
		p.fillRect(rect(), st::windowBgActive);
	});
}

void ForwardingChild::setLog(std::shared_ptr<ProbeLog> log) {
	_log = std::move(log);
}

void ForwardingChild::mousePressEvent(QMouseEvent *e) {
	RecordMouse(*_log, u"child"_q, parentWidget(), e);
	e->accept();
	forward(e);
}

void ForwardingChild::mouseMoveEvent(QMouseEvent *e) {
	RecordMouse(*_log, u"child"_q, parentWidget(), e);
	CountHeldMove(_log, u"child"_q, this, e);
	e->accept();
}

void ForwardingChild::mouseReleaseEvent(QMouseEvent *e) {
	RecordMouse(*_log, u"child"_q, parentWidget(), e);
	e->accept();
	forward(e);
}

void ForwardingChild::mouseDoubleClickEvent(QMouseEvent *e) {
	RecordMouse(*_log, u"child"_q, parentWidget(), e);
	e->accept();
}

void ForwardingChild::enterEventHook(QEnterEvent *e) {
	RecordHover(*_log, u"child"_q, parentWidget(), e);
	RpWidget::enterEventHook(e);
}

void ForwardingChild::leaveEventHook(QEvent *e) {
	RecordHover(*_log, u"child"_q, parentWidget(), e);
	RpWidget::leaveEventHook(e);
}

void ForwardingChild::forward(not_null<QMouseEvent*> e) {
	const auto parent = parentWidget();
	if (!parent) {
		return;
	}
	auto copy = QMouseEvent(
		e->type(),
		mapTo(parent, e->position()),
		e->scenePosition(),
		e->globalPosition(),
		e->button(),
		e->buttons(),
		e->modifiers(),
		e->pointingDevice());
	copy.setTimestamp(e->timestamp());
	QCoreApplication::sendEvent(parent, &copy);
}

GrabParent::GrabParent(QWidget *parent, ParentMode mode)
: RpWidget(parent)
, _mode(mode)
, _log(std::make_shared<ProbeLog>()) {
	_animation.init([=](crl::time now) {
		return tick(now);
	});
	paintOn([=](QPainter &p) {
		p.fillRect(rect(), st::windowBgOver);
	});
}

void GrabParent::setLog(std::shared_ptr<ProbeLog> log) {
	_animation.stop();
	_log = std::move(log);
}

void GrabParent::stopAnimation() {
	_animation.stop();
}

bool GrabParent::animating() const {
	return _animation.animating();
}

bool GrabParent::eventHook(QEvent *e) {
	if (_mode == ParentMode::SwallowQueuedCalls
		&& e->type() == QEvent::MetaCall) {
		++_log->swallowed;
		return true;
	}
	return RpWidget::eventHook(e);
}

void GrabParent::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		grabMouse();
	}
	RecordMouse(*_log, u"parent"_q, this, e);
	e->accept();
}

void GrabParent::mouseMoveEvent(QMouseEvent *e) {
	RecordMouse(*_log, u"parent"_q, this, e);
	if (_mode == ParentMode::DeleteAtMove) {
		QueueOwnDeletion(_log, this, e);
	} else {
		CountHeldMove(_log, u"parent"_q, this, e);
	}
	e->accept();
}

void GrabParent::mouseReleaseEvent(QMouseEvent *e) {
	RecordMouse(*_log, u"parent"_q, this, e);
	_log->releasedAt = crl::now();
	if (QWidget::mouseGrabber() == this) {
		releaseMouse();
	}
	_log->animationStartedAt = crl::now();
	_animation.start();
	e->accept();
}

void GrabParent::mouseDoubleClickEvent(QMouseEvent *e) {
	RecordMouse(*_log, u"parent"_q, this, e);
	e->accept();
}

void GrabParent::enterEventHook(QEnterEvent *e) {
	RecordHover(*_log, u"parent"_q, this, e);
	RpWidget::enterEventHook(e);
}

void GrabParent::leaveEventHook(QEvent *e) {
	RecordHover(*_log, u"parent"_q, this, e);
	RpWidget::leaveEventHook(e);
}

bool GrabParent::tick(crl::time now) {
	++_log->ticks;
	if (now - _animation.started() < kProbeAnimation) {
		return true;
	}
	_log->animationCompleted = true;
	_log->animationCompletedAt = now;
	return false;
}

LandingPad::LandingPad(QWidget *parent)
: RpWidget(parent)
, _log(std::make_shared<ProbeLog>()) {
	paintOn([=](QPainter &p) {
		p.fillRect(rect(), st::windowBgRipple);
	});
}

void LandingPad::setLog(std::shared_ptr<ProbeLog> log) {
	_log = std::move(log);
}

void LandingPad::mousePressEvent(QMouseEvent *e) {
	RecordMouse(*_log, u"pad"_q, nullptr, e);
	e->accept();
}

void LandingPad::mouseMoveEvent(QMouseEvent *e) {
	RecordMouse(*_log, u"pad"_q, nullptr, e);
	e->accept();
}

void LandingPad::mouseReleaseEvent(QMouseEvent *e) {
	RecordMouse(*_log, u"pad"_q, nullptr, e);
	e->accept();
}

void LandingPad::mouseDoubleClickEvent(QMouseEvent *e) {
	RecordMouse(*_log, u"pad"_q, nullptr, e);
	e->accept();
}

void LandingPad::enterEventHook(QEnterEvent *e) {
	RecordHover(*_log, u"pad"_q, nullptr, e);
	RpWidget::enterEventHook(e);
}

void LandingPad::leaveEventHook(QEvent *e) {
	RecordHover(*_log, u"pad"_q, nullptr, e);
	RpWidget::leaveEventHook(e);
}

// An object filter on a fixture QWindow: it runs after every application
// filter and records only what reached the window.
void InstallWindowLog(
		not_null<QWindow*> window,
		std::shared_ptr<WindowLog> log) {
	base::install_event_filter(window.get(), [=](not_null<QEvent*> e) {
		if (IsMouse(e->type())) {
			log->events.push_back({
				.type = e->type(),
				.spontaneous = e->spontaneous(),
			});
		}
		return base::EventFilterResult::Continue;
	});
}

[[nodiscard]] QString PlatformInputSkip() {
	return kPlatformInput
		? QString()
		: (u"unsupported: built with DESKTOP_APP_DISABLE_PRIVATE_QT_API, so "
			u"there is no QWindowSystemInterface delivery"_q);
}

[[nodiscard]] bool InFixture(const State &state, QWidget *widget) {
	const auto top = widget ? widget->window() : nullptr;
	return top && ((top == state.pairs.get()) || (top == state.pads.get()));
}

// The probe windows and every widget in them that Qt marks with
// Qt::WA_UnderMouse.
[[nodiscard]] QStringList FixtureHovered(const State &state) {
	auto result = QStringList();
	for (const auto top : { state.pairs.get(), state.pads.get() }) {
		auto widgets = top ? top->findChildren<QWidget*>() : QList<QWidget*>();
		if (top) {
			widgets.prepend(top);
		}
		for (const auto widget : widgets) {
			if (widget->underMouse()) {
				result.push_back(WidgetDescription(widget));
			}
		}
	}
	return result;
}

[[nodiscard]] EndState ReadEndState(const State &state) {
	return {
		.hovered = FixtureHovered(state),
		.grabber = Describe(QWidget::mouseGrabber()),
		.buttons = QGuiApplication::mouseButtons(),
		.read = true,
	};
}

[[nodiscard]] QString EndStateText(const EndState &state) {
	return u"buttons=%1 grabber=%2 hovered=[%3] clean=%4"_q.arg(
		QString::number(state.buttons.toInt()),
		DescriptionText(state.grabber),
		state.hovered.join(u"; "_q),
		FlagText(state.clean()));
}

[[nodiscard]] bool AnyAnimating(const State &state) {
	const auto subject = state.subject.parent.data();
	const auto control = state.control.parent.data();
	return (subject && subject->animating())
		|| (control && control->animating());
}

// The press point and Test::Drag's interpolated points after it, in a
// pair parent's coordinates; the last one is |to|, where the release goes.
[[nodiscard]] std::vector<QPoint> GesturePoints(QPoint from, QPoint to) {
	auto result = std::vector<QPoint>{ from };
	for (auto step = 1; step <= kSelfTestSteps; ++step) {
		result.push_back(from + ((to - from) * step) / kSelfTestSteps);
	}
	return result;
}

// A point of the pair's parent in global logical coordinates, read from
// its top-level's geometry and its offset there rather than from
// QWidget::mapToGlobal, which the helper itself uses.
[[nodiscard]] QPointF ExpectedGlobal(const ProbePair &pair, QPoint point) {
	const auto parent = pair.parent.data();
	if (!parent) {
		return QPointF();
	}
	const auto top = parent->window();
	return QPointF(top->geometry().topLeft() + parent->mapTo(top, point));
}

[[nodiscard]] ProbeEvent ExpectedMouse(
		const QString &who,
		QEvent::Type type,
		QPoint local,
		QPointF global,
		bool spontaneous,
		const QString &grabber) {
	const auto press = (type == QEvent::MouseButtonPress);
	const auto release = (type == QEvent::MouseButtonRelease);
	return {
		.who = who,
		.grabber = grabber,
		.local = QPointF(local),
		.global = global,
		.type = type,
		.button = (press || release) ? Qt::LeftButton : Qt::NoButton,
		.buttons = release
			? Qt::MouseButtons()
			: Qt::MouseButtons(Qt::LeftButton),
		.spontaneous = spontaneous,
	};
}

[[nodiscard]] ProbeEvent ExpectedLeave(const QString &who) {
	return { .who = who, .type = QEvent::Leave };
}

// The press both routes open with: the child under |from| takes it,
// spontaneous only through QPA, and sends its copy to the parent, which
// grabs on that copy.
void PushPress(
		std::vector<ProbeEvent> &result,
		const ProbePair &pair,
		QPoint from,
		bool spontaneous) {
	const auto offset = pair.child ? pair.child->pos() : QPoint();
	const auto global = ExpectedGlobal(pair, from);
	result.push_back(ExpectedMouse(
		u"child"_q,
		QEvent::MouseButtonPress,
		from - offset,
		global,
		spontaneous,
		u"none"_q));
	result.push_back(ExpectedMouse(
		u"parent"_q,
		QEvent::MouseButtonPress,
		from,
		global,
		false,
		u"parent"_q));
}

// The helper's documented sequence on the subject pair. Qt routes the
// press to the child under it, which forwards a sent copy to the parent;
// the parent grabs, so Qt sends every held move and the release to it,
// spontaneous, wherever they are. After the release Qt sends Leave to the
// pressed child and its ancestors below the widget under the release
// point (QApplicationPrivate::sendMouseEvent, leaveAfterRelease); end()'s
// QPA leave then reaches only the window.
[[nodiscard]] std::vector<ProbeEvent> SubjectExpected(const State &state) {
	const auto &pair = state.subject;
	const auto points = GesturePoints(state.from, state.to);
	const auto global = [&](QPoint point) {
		return ExpectedGlobal(pair, point);
	};
	const auto parent = u"parent"_q;
	auto result = std::vector<ProbeEvent>();
	PushPress(result, pair, state.from, true);
	for (auto i = 1; i != int(points.size()); ++i) {
		result.push_back(ExpectedMouse(
			parent,
			QEvent::MouseMove,
			points[i],
			global(points[i]),
			true,
			parent));
	}
	result.push_back(ExpectedMouse(
		parent,
		QEvent::MouseButtonRelease,
		state.to,
		global(state.to),
		true,
		parent));
	result.push_back(ExpectedLeave(u"child"_q));
	result.push_back(ExpectedLeave(parent));
	return result;
}

// Test::Drag's documented sequence aimed at the control child - a press,
// eight held moves, a release and the leave, all sent - with the child's
// forwarded copies of the press and the release reaching the parent,
// which holds its grab through the moves the child takes.
[[nodiscard]] std::vector<ProbeEvent> ControlExpected(const State &state) {
	const auto &pair = state.control;
	const auto offset = pair.child ? pair.child->pos() : QPoint();
	const auto points = GesturePoints(state.from, state.to);
	const auto global = [&](QPoint point) {
		return ExpectedGlobal(pair, point);
	};
	const auto child = u"child"_q;
	const auto parent = u"parent"_q;
	auto result = std::vector<ProbeEvent>();
	PushPress(result, pair, state.from, false);
	for (auto i = 1; i != int(points.size()); ++i) {
		result.push_back(ExpectedMouse(
			child,
			QEvent::MouseMove,
			points[i] - offset,
			global(points[i]),
			false,
			parent));
	}
	result.push_back(ExpectedMouse(
		child,
		QEvent::MouseButtonRelease,
		state.to - offset,
		global(state.to),
		false,
		parent));
	result.push_back(ExpectedMouse(
		parent,
		QEvent::MouseButtonRelease,
		state.to,
		global(state.to),
		false,
		parent));
	result.push_back(ExpectedLeave(child));
	return result;
}

// "<who>:<kind> <x>,<y> button=<int> buttons=<int> modifiers=<int>
// spontaneous=<0|1> grabber=<none|parent|other>" for a mouse event, with
// the local position rounded, and "<who>:<kind>" for an enter or a leave.
[[nodiscard]] QString EventLiteral(const ProbeEvent &event) {
	const auto kind = event.who + u":"_q + KindName(event.type);
	if (!IsMouse(event.type)) {
		return kind;
	}
	return u"%1 %2,%3 button=%4 buttons=%5 modifiers=%6 spontaneous=%7 "
		u"grabber=%8"_q.arg(
			kind,
			QString::number(int(base::SafeRound(event.local.x()))),
			QString::number(int(base::SafeRound(event.local.y()))),
			QString::number(int(event.button)),
			QString::number(event.buttons.toInt()),
			QString::number(event.modifiers.toInt()),
			FlagText(event.spontaneous),
			event.grabber);
}

[[nodiscard]] QStringList Literals(const std::vector<ProbeEvent> &events) {
	auto result = QStringList();
	for (const auto &event : events) {
		result.push_back(EventLiteral(event));
	}
	return result;
}

[[nodiscard]] std::vector<ProbeEvent> MouseEvents(
		const std::vector<ProbeEvent> &events,
		const QString &who = QString()) {
	auto result = std::vector<ProbeEvent>();
	for (const auto &event : events) {
		if (IsMouse(event.type) && (who.isEmpty() || (event.who == who))) {
			result.push_back(event);
		}
	}
	return result;
}

[[nodiscard]] int CountEvents(
		const ProbeLog &log,
		const QString &who,
		QEvent::Type type) {
	return int(std::count_if(
		begin(log.events),
		end(log.events),
		[&](const ProbeEvent &event) {
			return (event.who == who) && (event.type == type);
		}));
}

// The largest per-axis distance between a received mouse event's local
// or global position and the expected one at the same index among the
// mouse events; -1 when the two lists hold different numbers of them.
[[nodiscard]] double MaxPositionError(
		const std::vector<ProbeEvent> &received,
		const std::vector<ProbeEvent> &expected) {
	const auto got = MouseEvents(received);
	const auto want = MouseEvents(expected);
	if (got.size() != want.size()) {
		return -1.;
	}
	auto result = 0.;
	for (auto i = 0; i != int(got.size()); ++i) {
		const auto local = got[i].local - want[i].local;
		const auto global = got[i].global - want[i].global;
		result = std::max({
			result,
			std::abs(local.x()),
			std::abs(local.y()),
			std::abs(global.x()),
			std::abs(global.y()),
		});
	}
	return result;
}

// Each mouse event's received and expected positions, side by side.
[[nodiscard]] QString PositionsText(
		const std::vector<ProbeEvent> &received,
		const std::vector<ProbeEvent> &expected) {
	const auto got = MouseEvents(received);
	const auto want = MouseEvents(expected);
	auto parts = QStringList();
	for (auto i = 0; i != int(got.size()); ++i) {
		const auto &event = got[i];
		const auto other = (i < int(want.size())) ? want[i] : ProbeEvent();
		parts.push_back(u"%1:%2 local=%3/%4 global=%5/%6"_q.arg(
			event.who,
			KindName(event.type),
			PointText(event.local),
			PointText(other.local),
			PointText(event.global),
			PointText(other.global)));
	}
	return u"received/expected=[%1]"_q.arg(parts.join(u"; "_q));
}

[[nodiscard]] QStringList WindowLiterals(
		const std::vector<WindowEvent> &events) {
	auto result = QStringList();
	for (const auto &event : events) {
		result.push_back(
			KindName(event.type) + u":"_q + FlagText(event.spontaneous));
	}
	return result;
}

// What the window gets from one QPA gesture: the press, the held moves
// and the release, every one spontaneous and no double click.
[[nodiscard]] QStringList ExpectedWindow() {
	auto result = QStringList{ u"press:1"_q };
	for (auto i = 0; i != kSelfTestSteps; ++i) {
		result.push_back(u"move:1"_q);
	}
	result.push_back(u"release:1"_q);
	return result;
}

// Every mouse event a probe got, as "<who>:<kind>:<spontaneous>".
[[nodiscard]] QString SpontaneityText(const std::vector<ProbeEvent> &events) {
	auto parts = QStringList();
	for (const auto &event : MouseEvents(events)) {
		parts.push_back(u"%1:%2:%3"_q.arg(
			event.who,
			KindName(event.type),
			FlagText(event.spontaneous)));
	}
	return parts.join(u", "_q);
}

[[nodiscard]] QString SawReleaseText(const std::optional<bool> &value) {
	return !value ? u"unset"_q : FlagText(*value);
}

[[nodiscard]] QString PairLogText(const QString &label, const ProbeLog &log) {
	return u"%1: heldMoves=%2 postedBy=%3 queuedSawRelease=%4 "
		u"mainQueueSawRelease=%5 released=%6 order=[%7]"_q.arg(
			label,
			QString::number(log.heldMoves),
			DescriptionText(log.postedBy),
			SawReleaseText(log.queuedSawRelease),
			SawReleaseText(log.mainQueueSawRelease),
			FlagText(log.released),
			log.order.join(u", "_q));
}

[[nodiscard]] int ReleaseIndex(const ProbeLog &log) {
	for (auto i = 0; i != int(log.order.size()); ++i) {
		if (log.order[i].endsWith(u":release"_q)) {
			return i;
		}
	}
	return -1;
}

// The pair's own |kind| call ran, saw no release, and sits before the
// first release in the pair's order.
[[nodiscard]] bool RanBeforeRelease(
		const ProbeLog &log,
		const std::optional<bool> &sawRelease,
		const QString &kind) {
	const auto call = int(log.order.indexOf(kind));
	const auto release = ReleaseIndex(log);
	return sawRelease.has_value()
		&& !*sawRelease
		&& (call >= 0)
		&& (release >= 0)
		&& (call < release);
}

// The pair's own |kind| call ran after a release reached the pair, and saw
// it.
[[nodiscard]] bool RanAfterRelease(
		const ProbeLog &log,
		const std::optional<bool> &sawRelease,
		const QString &kind) {
	const auto call = int(log.order.indexOf(kind));
	const auto release = ReleaseIndex(log);
	return sawRelease.has_value()
		&& *sawRelease
		&& (release >= 0)
		&& (call > release);
}

[[nodiscard]] crl::time ObservedFrom(const ProbeLog &log, crl::time since) {
	return log.releasedAt ? log.releasedAt : since;
}

// The animation ended, or kAnimationBound passed since the parent's
// release - since |since| when no release reached it.
[[nodiscard]] bool AnimationSettled(const ProbeLog &log, crl::time since) {
	return log.animationCompleted
		|| (crl::now() - ObservedFrom(log, since) >= kAnimationBound);
}

[[nodiscard]] QString AnimationText(const ProbeLog &log, crl::time since) {
	const auto completed = log.animationCompleted;
	return u"released=%1 completed=%2 ticks=%3 elapsedMs=%4 durationMs=%5 "
		u"observedMs=%6 probeMs=%7 boundMs=%8"_q.arg(
			FlagText(log.releasedAt > 0),
			FlagText(completed),
			QString::number(log.ticks),
			(completed
				? QString::number(log.animationCompletedAt - log.releasedAt)
				: u"none"_q),
			(completed
				? QString::number(
					log.animationCompletedAt - log.animationStartedAt)
				: u"none"_q),
			QString::number(crl::now() - ObservedFrom(log, since)),
			QString::number(kProbeAnimation),
			QString::number(kAnimationBound));
}

// "QPA_MOUSE_INGREDIENT: leg=<both|raw|settle|wait> settle=<0|1>
// wait=<0|1> <AnimationText> <markers> queuedSawRelease=<0|1|unset>
// mainQueueSawRelease=<0|1|unset>": one leg's observation, never judged.
[[nodiscard]] QString IngredientRow(
		const QString &leg,
		bool settle,
		bool wait,
		const ProbeLog &log,
		crl::time since,
		const QString &markers) {
	return u"QPA_MOUSE_INGREDIENT: leg=%1 settle=%2 wait=%3 %4 %5 "
		u"queuedSawRelease=%6 mainQueueSawRelease=%7"_q.arg(
			leg,
			FlagText(settle),
			FlagText(wait),
			AnimationText(log, since),
			markers.isEmpty() ? u"markers=none"_q : markers,
			SawReleaseText(log.queuedSawRelease),
			SawReleaseText(log.mainQueueSawRelease));
}

[[nodiscard]] QString IngredientSummary(const ProbeLog *log) {
	if (!log) {
		return u"not run"_q;
	}
	return u"completed=%1 ticks=%2 elapsedMs=%3"_q.arg(
		FlagText(log->animationCompleted),
		QString::number(log->ticks),
		(log->animationCompleted
			? QString::number(log->animationCompletedAt - log->releasedAt)
			: u"none"_q));
}

[[nodiscard]] QString DragMarkersText(const QpaMouseDrag &drag) {
	return u"qtQueue=%1 mainQueue=%2 waitedMs=%3"_q.arg(
		FlagText(drag.qtQueueRan),
		FlagText(drag.mainQueueRan),
		QString::number(drag.waitedMs));
}

// Qt's logical-to-native factor for |window| (QHighDpiScaling::factor):
// QT_SCALE_FACTOR on macOS, not the backing devicePixelRatio. Zero when
// it cannot be read.
[[nodiscard]] double ConversionFactor(QWindow *window) {
#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
	return window ? QHighDpiScaling::factor(window) : 0.;
#else // !DESKTOP_APP_DISABLE_PRIVATE_QT_API
	return 0.;
#endif // DESKTOP_APP_DISABLE_PRIVATE_QT_API
}

[[nodiscard]] QString FactorText(QWindow *window) {
	return !kPlatformInput
		? u"unavailable"_q
		: window
		? NumberText(ConversionFactor(window))
		: u"none"_q;
}

[[nodiscard]] QString GlobalRectText(QWidget *widget) {
	return widget
		? RectText(QRect(widget->mapToGlobal(QPoint()), widget->size()))
		: u"none"_q;
}

void SetPairLog(ProbePair &pair, std::shared_ptr<ProbeLog> log) {
	if (const auto parent = pair.parent.data()) {
		parent->setLog(log);
	}
	if (const auto child = pair.child.data()) {
		child->setLog(log);
	}
	pair.log = std::move(log);
}

// A parentless Ui::RpWidget is a Qt::Window: shown without activating
// (Qt::WA_ShowWithoutActivating set before its QWindow exists, and never
// raise()) and with Qt::WA_QuitOnClose cleared, as MakeOrdinaryWindow in
// test_window_exposure.cpp.
[[nodiscard]] base::unique_qptr<Ui::RpWidget> MakeWindow(
		const QString &name,
		QSize size) {
	auto result = base::make_unique_q<Ui::RpWidget>(nullptr);
	const auto window = result.get();
	window->setObjectName(u"qpa_mouse_self_test_"_q + name);
	window->setWindowTitle(u"Harness QPA mouse "_q + name);
	window->setAttribute(Qt::WA_ShowWithoutActivating);
	window->setAttribute(Qt::WA_QuitOnClose, false);
	window->setAttribute(Qt::WA_OpaquePaintEvent);
	window->paintOn([=](QPainter &p) {
		p.fillRect(window->rect(), st::windowBg);
	});
	window->resize(size);
	return result;
}

// A GrabParent of two by two cells at |origin| of |top|, and its
// ForwardingChild of one cell, half a cell in from the parent's corner.
void BuildPair(
		ProbePair &pair,
		not_null<QWidget*> top,
		QPoint origin,
		int cell,
		std::shared_ptr<ProbeLog> log,
		ParentMode mode = ParentMode::Record) {
	const auto parent = Ui::CreateChild<GrabParent>(top.get(), mode);
	parent->setGeometry(QRect(origin, QSize(2 * cell, 2 * cell)));
	const auto child = Ui::CreateChild<ForwardingChild>(parent);
	child->setGeometry(cell / 2, cell / 2, cell, cell);
	parent->show();
	child->show();
	pair.parent = parent;
	pair.child = child;
	SetPairLog(pair, std::move(log));
}

// The corner of |available| where a |size| box's centre lies farthest
// from |cursor|.
[[nodiscard]] QPoint FarCorner(
		const QRect &available,
		QSize size,
		QPoint cursor) {
	const auto right = available.x() + available.width() - size.width();
	const auto bottom = available.y() + available.height() - size.height();
	const auto corners = std::array{
		available.topLeft(),
		QPoint(right, available.y()),
		QPoint(available.x(), bottom),
		QPoint(right, bottom),
	};
	auto result = corners.front();
	auto distance = -1.;
	for (const auto &corner : corners) {
		const auto center = QRectF(QPointF(corner), QSizeF(size)).center();
		const auto now = std::hypot(
			center.x() - cursor.x(),
			center.y() - cursor.y());
		if (now > distance) {
			distance = now;
			result = corner;
		}
	}
	return result;
}

// Stacks |windows| in one column, each below room for its title bar, in
// the corner of |available| farthest from |cursor|.
[[nodiscard]] Placement PlaceColumn(
		const std::vector<not_null<QWidget*>> &windows,
		const QRect &available,
		QPoint cursor) {
	const auto titleRoom = st::defaultActiveButton.height;
	auto column = QSize();
	for (const auto window : windows) {
		const auto size = window->size();
		column = QSize(
			std::max(column.width(), size.width()),
			column.height() + titleRoom + size.height());
	}
	auto position = FarCorner(available, column, cursor);
	const auto bounds = QRect(position, column);
	for (const auto window : windows) {
		const auto size = window->size();
		position.ry() += titleRoom;
		window->setGeometry(QRect(position, size));
		position.ry() += size.height();
	}
	const auto fits = available.contains(bounds);
	return {
		.text = u"screen=%1 column=%2 fits=%3"_q.arg(
			RectText(available),
			RectText(bounds),
			FlagText(fits)),
		.fits = fits,
	};
}

[[nodiscard]] QScreen *FixtureScreen(QPoint cursor) {
	const auto screen = QGuiApplication::screenAt(cursor);
	return screen ? screen : QGuiApplication::primaryScreen();
}

// The refusal that fails the fixture gate, or empty with both windows
// built, placed and shown. Every size comes from the live style: the cell
// is st::defaultActiveButton.height. The pairs window holds the subject
// and the control pair, one per row, each with room for the end point on
// its right; the pad window holds the landing pad.
[[nodiscard]] QString BuildFixture(not_null<State*> state) {
	const auto cursor = QCursor::pos();
	const auto screen = FixtureScreen(cursor);
	if (!screen) {
		return u"no screen: neither one at the cursor nor a primary one"_q;
	}
	const auto cell = st::defaultActiveButton.height;
	const auto gap = cell / 2;
	const auto pair = 2 * cell;
	const auto width = gap + 4 * cell + gap;
	state->pairsWindow = std::make_shared<WindowLog>();
	state->padsWindow = std::make_shared<WindowLog>();
	state->pairs = MakeWindow(
		u"pairs"_q,
		QSize(width, gap + pair + gap + pair + gap));
	state->subjectLog = std::make_shared<ProbeLog>();
	BuildPair(
		state->subject,
		state->pairs.get(),
		QPoint(gap, gap),
		cell,
		state->subjectLog);
	state->cell = cell;
	state->spareOrigin = QPoint(gap, gap + pair + gap);
	BuildPair(
		state->control,
		state->pairs.get(),
		state->spareOrigin,
		cell,
		std::make_shared<ProbeLog>());
	state->pads = MakeWindow(u"pad"_q, QSize(width, gap + pair + gap));
	const auto pad = Ui::CreateChild<LandingPad>(state->pads.get());
	pad->setGeometry(gap, gap, 4 * cell, pair);
	state->padLog = std::make_shared<ProbeLog>();
	pad->setLog(state->padLog);
	pad->show();
	state->pad = pad;

	// From the child's centre, in the parent's coordinates, to one cell
	// right of the parent: outside the child and the parent, inside the
	// window, over no other widget.
	state->from = state->subject.child->geometry().center();
	state->to = QPoint(pair + cell, state->from.y());

	const auto placement = PlaceColumn(
		{ state->pairs.get(), state->pads.get() },
		screen->availableGeometry(),
		cursor);
	state->placement = placement.text;
	state->fits = placement.fits;
	state->pairs->show();
	state->pads->show();
	if (const auto handle = state->pairs->windowHandle()) {
		InstallWindowLog(handle, state->pairsWindow);
	}
	if (const auto handle = state->pads->windowHandle()) {
		InstallWindowLog(handle, state->padsWindow);
	}
	return QString();
}

// Readiness only, never a success: a shown QWindow.
[[nodiscard]] bool WindowReady(QWidget *top) {
	const auto handle = top ? top->windowHandle() : nullptr;
	return handle && handle->isVisible();
}

// Empty when the gesture's points suit the legs: all distinct, a held move
// outside the child and one outside the parent, and an end point outside
// both, inside the window, over no other widget.
[[nodiscard]] QString PointsRefusal(const State &state) {
	const auto parent = state.subject.parent.data();
	const auto child = state.subject.child.data();
	const auto top = state.pairs.get();
	if (!parent || !child || !top) {
		return u"the subject pair is gone"_q;
	}
	const auto points = GesturePoints(state.from, state.to);
	auto distinct = true;
	for (auto i = 0; i != int(points.size()); ++i) {
		for (auto j = i + 1; j != int(points.size()); ++j) {
			if (points[i] == points[j]) {
				distinct = false;
			}
		}
	}
	auto outsideChild = 0;
	auto outsideParent = 0;
	for (auto i = 1; i != int(points.size()); ++i) {
		if (!child->geometry().contains(points[i])) {
			++outsideChild;
		}
		if (!parent->rect().contains(points[i])) {
			++outsideParent;
		}
	}
	const auto end = parent->mapTo(top, state.to);
	const auto endOutside = !child->geometry().contains(state.to)
		&& !parent->rect().contains(state.to)
		&& top->rect().contains(end)
		&& !top->childAt(end);
	if (distinct && outsideChild && outsideParent && endOutside) {
		return QString();
	}
	return u"distinct=%1 movesOutsideChild=%2 movesOutsideParent=%3 "
		u"endOutsideInWindow=%4"_q.arg(
			FlagText(distinct),
			QString::number(outsideChild),
			QString::number(outsideParent),
			FlagText(endOutside));
}

[[nodiscard]] bool CursorOverFixture(const State &state, QPoint cursor) {
	const auto pairs = state.pairs.get();
	const auto pads = state.pads.get();
	return (pairs && pairs->geometry().contains(cursor))
		|| (pads && pads->geometry().contains(cursor));
}

// The fixture row: Qt version, the pairs window's devicePixelRatio and
// Qt's actual conversion factor (QHighDpiScaling::factor), the
// double-click interval and distance, the animation mode, the cursor and
// every geometry, global and logical. |pairsOrigin| is the window's exact
// global origin, to compare with the integer geometry ExpectedGlobal
// reads.
[[nodiscard]] QString FixtureText(const State &state) {
	const auto pairs = state.pairs.get();
	const auto handle = pairs ? pairs->windowHandle() : nullptr;
	const auto hints = QGuiApplication::styleHints();
	const auto cursor = QCursor::pos();
	return u"QPA_MOUSE_SELF_TEST: fixture qt=%1 platformInput=%2 dpr=%3 "
		u"factor=%4 doubleClickInterval=%5 doubleClickDistance=%6 "
		u"animation=[disabled=%7 slowMultiplier=%8 probeMs=%9 boundMs=%10] "
		u"cursor=%11 cursorOverFixture=%12 %13 pairs=%14 pads=%15 "
		u"subject=[parent=%16 child=%17] control=[parent=%18 child=%19] "
		u"pad=%20 from=%21 to=%22 fromGlobal=%23 toGlobal=%24 "
		u"pairsOrigin=%25"_q.arg(
			QString::fromLatin1(qVersion()),
			FlagText(kPlatformInput),
			handle ? NumberText(handle->devicePixelRatio()) : u"none"_q,
			FactorText(handle),
			QString::number(hints->mouseDoubleClickInterval()),
			QString::number(hints->mouseDoubleClickDistance()),
			FlagText(anim::Disabled()),
			QString::number(anim::SlowMultiplier()),
			QString::number(kProbeAnimation),
			QString::number(kAnimationBound),
			PointText(cursor),
			FlagText(CursorOverFixture(state, cursor)),
			state.placement,
			GlobalRectText(pairs),
			GlobalRectText(state.pads.get()),
			GlobalRectText(state.subject.parent.data()),
			GlobalRectText(state.subject.child.data()),
			GlobalRectText(state.control.parent.data()),
			GlobalRectText(state.control.child.data()),
			GlobalRectText(state.pad.data()),
			PointText(state.from),
			PointText(state.to),
			PointText(ExpectedGlobal(state.subject, state.from)),
			PointText(ExpectedGlobal(state.subject, state.to)),
			(pairs
				? PointText(pairs->QWidget::mapToGlobal(QPointF()))
				: u"none"_q));
}

// Empty when every leg can start: both windows shown and inside the
// screen, usable points, and Qt holding no button, grab, hover, popup or
// modal of its own.
[[nodiscard]] QString FixtureRefusal(const State &state) {
	if (!state.buildRefusal.isEmpty()) {
		return state.buildRefusal;
	} else if (!WindowReady(state.pairs.get())
		|| !WindowReady(state.pads.get())) {
		return u"the probe windows are not shown"_q;
	} else if (!state.fits) {
		return u"the probe windows do not fit the available screen "
			u"geometry: "_q + state.placement;
	}
	const auto points = PointsRefusal(state);
	if (!points.isEmpty()) {
		return u"the gesture's points do not suit the legs: "_q + points;
	}
	const auto start = ReadEndState(state);
	if (!start.clean()) {
		return u"Qt already holds pointer state: "_q + EndStateText(start);
	} else if (const auto popup = QApplication::activePopupWidget()) {
		return u"a popup is open: "_q + WidgetDescription(popup);
	} else if (const auto modal = QApplication::activeModalWidget()) {
		return u"a modal window is open: "_q + WidgetDescription(modal);
	}
	return QString();
}

// Every log starts empty right here, in the turn whose next stage is the
// subject's press.
void FinishFixture(not_null<State*> state) {
	const auto refusal = FixtureRefusal(*state);
	state->gate = refusal.isEmpty()
		? QString()
		: (u"fixture gate: "_q + refusal);
	Note(FixtureText(*state));
	Check(
		refusal.isEmpty(),
		u"fixture gate: the QPA mouse self-test's probe windows are shown "
		u"inside the screen away from the cursor, and Qt holds no button, "
		u"grab, hover, popup or modal"_q,
		refusal.isEmpty() ? state->placement : refusal);
	if (!refusal.isEmpty()) {
		return;
	}
	state->subjectLog = std::make_shared<ProbeLog>();
	SetPairLog(state->subject, state->subjectLog);
	SetPairLog(state->control, std::make_shared<ProbeLog>());
	state->padLog = std::make_shared<ProbeLog>();
	if (const auto pad = state->pad.data()) {
		pad->setLog(state->padLog);
	}
	state->pairsWindow->events.clear();
	state->padsWindow->events.clear();
}

[[nodiscard]] int FixtureWindows(const State &state) {
	return (state.pairs ? 1 : 0)
		+ (state.pads ? 1 : 0)
		+ (state.refused.unshown ? 1 : 0)
		+ (state.refused.offscreen ? 1 : 0);
}

void DestroyFixture(State &state) {
	state.pairs = nullptr;
	state.pads = nullptr;
	state.refused.unshown = nullptr;
	state.refused.offscreen = nullptr;
}

// Releases the console shield only while the self-test's own arm holds
// it, so no cleanup here ever ends an arm another self-test measures.
bool ReleaseOwnedShield(const QString &by) {
	const auto reading = ReadConsoleShield();
	return reading.armed
		&& (reading.label == kShieldLabel)
		&& ReleaseConsoleShield(by);
}

[[nodiscard]] QpaMouseDragTarget SubjectTarget(const State &state) {
	return {
		.widget = state.subject.parent.data(),
		.from = state.from,
		.to = state.to,
	};
}

// The subject's route checks, against the reading taken right after the
// gesture's cleanup: |route| is what the pair got, |window| what its
// QWindow got, |drag| the helper's reading and |end| Qt's pointer state.
void CheckSubjectRoute(
		const State &state,
		const QString &label,
		const ProbeLog &route,
		const std::vector<WindowEvent> &window,
		const QpaMouseDrag &drag,
		const EndState &end) {
	const auto expected = SubjectExpected(state);
	const auto child = MouseEvents(route.events, u"child"_q);
	const auto parent = MouseEvents(route.events, u"parent"_q);
	const auto childRect = state.subject.child
		? state.subject.child->geometry()
		: QRect();
	const auto parentRect = state.subject.parent
		? state.subject.parent->rect()
		: QRect();
	auto grabbedMoves = 0;
	auto outsideChild = 0;
	auto outsideParent = 0;
	for (const auto &event : parent) {
		if (event.type != QEvent::MouseMove) {
			continue;
		}
		if ((event.buttons == Qt::LeftButton)
			&& (event.grabber == u"parent"_q)) {
			++grabbedMoves;
		}
		if (!childRect.contains(event.local.toPoint())) {
			++outsideChild;
		}
		if (!parentRect.contains(event.local.toPoint())) {
			++outsideParent;
		}
	}
	Check(
		(child.size() == 1)
			&& (child.front().type == QEvent::MouseButtonPress)
			&& (parent.size() == kSelfTestSteps + 2)
			&& (parent.front().type == QEvent::MouseButtonPress)
			&& (parent.front().grabber == u"parent"_q)
			&& (grabbedMoves == kSelfTestSteps)
			&& (parent.back().type == QEvent::MouseButtonRelease)
			&& (parent.back().grabber == u"parent"_q)
			&& (outsideChild > 0)
			&& (outsideParent > 0),
		label + u": the press reached only the child, which forwarded it; "
			u"the parent grabbed on that copy and took all eight held moves, "
			u"outside the child and outside itself too, and the release "
			u"through its grab"_q,
		u"childMouseEvents=%1 parentMouseEvents=%2 grabbedHeldMoves=%3 "
		u"movesOutsideChild=%4 movesOutsideParent=%5"_q.arg(
			QString::number(child.size()),
			QString::number(parent.size()),
			QString::number(grabbedMoves),
			QString::number(outsideChild),
			QString::number(outsideParent)));

	const auto error = MaxPositionError(route.events, expected);
	Check(
		(error >= 0.) && (error <= kPositionTolerance),
		label + u": every press, move and release carries the requested "
			u"logical local and global position, within 0.01 px"_q,
		u"maxError=%1 %2"_q.arg(
			NumberText(error),
			PositionsText(route.events, expected)));

	const auto received = Literals(route.events);
	const auto documented = Literals(expected);
	Check(
		received == documented,
		label + u": the probes received exactly the documented sequence"_q,
		u"documented=[%1] received=[%2]"_q.arg(
			documented.join(u"; "_q),
			received.join(u"; "_q)));

	auto receiversSpontaneous = (child.size() == 1)
		&& child.front().spontaneous
		&& (parent.size() == kSelfTestSteps + 2)
		&& !parent.front().spontaneous;
	for (auto i = 1; i < int(parent.size()); ++i) {
		receiversSpontaneous = receiversSpontaneous && parent[i].spontaneous;
	}
	const auto windowLiterals = WindowLiterals(window);
	Check(
		(windowLiterals == ExpectedWindow()) && receiversSpontaneous,
		label + u": QPA input is spontaneous at the window and at its first "
			u"receiver; only the child's forwarded copy of the press reaches "
			u"the parent sent"_q,
		u"window=[%1] probes=[%2]"_q.arg(
			windowLiterals.join(u", "_q),
			SpontaneityText(route.events)));

	Check(
		(route.postedBy == u"parent"_q)
			&& RanBeforeRelease(route, route.queuedSawRelease, u"queued"_q)
			&& RanBeforeRelease(
				route,
				route.mainQueueSawRelease,
				u"main-queue"_q),
		label + u": the pair's own InvokeQueued and crl::on_main calls, "
			u"posted from held move 2, both ran before the release"_q,
		PairLogText(u"pair"_q, route));

	const auto childText = Describe(state.subject.child.data());
	const auto parentText = Describe(state.subject.parent.data());
	Check(
		drag.delivered()
			&& drag.pressed
			&& (drag.moves == kSelfTestSteps)
			&& drag.released
			&& drag.qtQueueRan
			&& drag.mainQueueRan
			&& (drag.pressReceiver == childText)
			&& (drag.grabber == parentText)
			&& (drag.releaseReceiver == parentText)
			&& drag.end.clean()
			&& !drag.end.releaseSent
			&& !drag.end.buttonsCleared
			&& !drag.end.grabReleased
			&& drag.end.leaveSent,
		label + u": the helper's reading: delivered with both markers run, "
			u"the child took the press, the parent held the grab and took "
			u"the release, and end() needed only its leave"_q,
		QpaMouseDragText(drag));

	Check(
		end.clean(),
		label + u": right after the gesture's cleanup Qt holds no button, "
			u"no grab and no hovered fixture widget"_q,
		EndStateText(end));
}

// Right after the drag stage's then, in the same turn: Qt's end state and
// a snapshot of the route, judged before any later event.
void ReadSubjectLeg(not_null<State*> state) {
	state->subjectObservedFrom = crl::now();
	state->subjectEnd = ReadEndState(*state);
	state->subjectRoute = *state->subjectLog;
	state->subjectWindow = state->pairsWindow->events;
	Note(u"QPA mouse self-test: subject: end state right after the "
		u"gesture's cleanup: %1"_q.arg(EndStateText(state->subjectEnd)));
	CheckSubjectRoute(
		*state,
		u"QPA mouse self-test: subject"_q,
		state->subjectRoute,
		state->subjectWindow,
		*state->subjectDrag,
		state->subjectEnd);
}

void CheckSubjectAnimation(not_null<State*> state) {
	const auto &log = *state->subjectLog;
	const auto laterProbe = int(log.events.size())
		- int(state->subjectRoute.events.size());
	const auto laterWindow = int(state->pairsWindow->events.size())
		- int(state->subjectWindow.size());
	Check(
		(log.releasedAt > 0)
			&& log.animationCompleted
			&& (log.ticks > 0)
			&& (log.animationCompletedAt - log.releasedAt <= kAnimationBound)
			&& (log.animationCompletedAt - log.animationStartedAt
				>= kProbeAnimation)
			&& !laterProbe
			&& !laterWindow,
		u"QPA mouse self-test: subject: the animation its release started "
		u"ticked to its end within 2000 ms of the release, with no later "
		u"input"_q,
		u"%1 laterProbeEvents=%2 laterWindowEvents=%3"_q.arg(
			AnimationText(log, state->subjectObservedFrom),
			QString::number(laterProbe),
			QString::number(laterWindow)));
	if (const auto parent = state->subject.parent.data()) {
		parent->stopAnimation();
	}
	state->subjectRow = IngredientRow(
		u"both"_q,
		true,
		true,
		log,
		state->subjectObservedFrom,
		DragMarkersText(*state->subjectDrag));
	Note(state->subjectRow);
}

// Test::Drag aimed at the control child with the subject's points mapped
// into it. Its route and Qt's end state are read as soon as it returns,
// before its pair's queued calls can run.
void RunControl(not_null<State*> state) {
	state->pairsWindow->events.clear();
	state->controlStarted = crl::now();
	const auto child = state->control.child.data();
	if (!child) {
		return;
	}
	const auto offset = child->pos();
	Drag(child, state->from - offset, state->to - offset, kSelfTestSteps);
	const auto &log = *state->control.log;
	state->controlRanBeforeReturn = log.queuedSawRelease.has_value()
		|| log.mainQueueSawRelease.has_value();
	state->controlRoute = log;
	state->controlWindow = state->pairsWindow->events;
	state->controlAfterDrag = ReadEndState(*state);
}

[[nodiscard]] bool ControlSettled(const State &state) {
	const auto &log = *state.control.log;
	const auto calls = (log.queuedSawRelease.has_value()
			&& log.mainQueueSawRelease.has_value())
		|| (crl::now() - state.controlStarted > kControlWait);
	return calls && AnimationSettled(log, state.controlStarted);
}

void CheckControl(not_null<State*> state) {
	const auto label = u"QPA mouse self-test: Test::Drag control"_q;
	const auto &route = state->controlRoute;
	const auto &log = *state->control.log;
	const auto expected = ControlExpected(*state);
	const auto received = Literals(route.events);
	const auto documented = Literals(expected);
	Check(
		received == documented,
		label + u": the child gets exactly Test::Drag's documented press, "
			u"eight held moves, release and leave, and the parent only the "
			u"child's forwarded press and release"_q,
		u"documented=[%1] received=[%2] maxPositionError=%3"_q.arg(
			documented.join(u"; "_q),
			received.join(u"; "_q),
			NumberText(MaxPositionError(route.events, expected))));

	const auto childMoves = CountEvents(route, u"child"_q, QEvent::MouseMove);
	const auto parentMoves = CountEvents(
		route,
		u"parent"_q,
		QEvent::MouseMove);
	const auto &subject = state->subjectRoute;
	const auto subjectChildMoves = CountEvents(
		subject,
		u"child"_q,
		QEvent::MouseMove);
	const auto subjectParentMoves = CountEvents(
		subject,
		u"parent"_q,
		QEvent::MouseMove);
	const auto subjectOpposite = !kPlatformInput
		|| ((subjectParentMoves == kSelfTestSteps)
			&& (subjectChildMoves == 0)
			&& (int(state->subjectWindow.size()) == kSelfTestSteps + 2));
	Check(
		(childMoves == kSelfTestSteps)
			&& (parentMoves == 0)
			&& state->controlWindow.empty()
			&& subjectOpposite,
		label + u": the route is the opposite of the helper's: the child "
			u"gets the eight moves and the parent none, with no event at "
			u"the window, where the helper gave the parent all eight "
			u"through its grab"_q,
		u"control: childMoves=%1 parentMoves=%2 windowEvents=%3; helper: "
		u"childMoves=%4 parentMoves=%5 windowEvents=%6%7"_q.arg(
			QString::number(childMoves),
			QString::number(parentMoves),
			QString::number(state->controlWindow.size()),
			QString::number(subjectChildMoves),
			QString::number(subjectParentMoves),
			QString::number(state->subjectWindow.size()),
			kPlatformInput
				? QString()
				: (u" (not compared: "_q + PlatformInputSkip() + u")"_q)));

	const auto subjectBefore = !kPlatformInput
		|| (RanBeforeRelease(
				subject,
				subject.queuedSawRelease,
				u"queued"_q)
			&& RanBeforeRelease(
				subject,
				subject.mainQueueSawRelease,
				u"main-queue"_q));
	Check(
		!state->controlRanBeforeReturn
			&& (log.postedBy == u"child"_q)
			&& RanAfterRelease(log, log.queuedSawRelease, u"queued"_q)
			&& RanAfterRelease(
				log,
				log.mainQueueSawRelease,
				u"main-queue"_q)
			&& subjectBefore,
		label + u": neither of the pair's own calls had run when Test::Drag "
			u"returned; both ran later and saw the release, where under the "
			u"helper both ran before it"_q,
		u"ranBeforeDragReturned=%1 %2; %3"_q.arg(
			FlagText(state->controlRanBeforeReturn),
			PairLogText(u"control"_q, log),
			PairLogText(u"helper"_q, subject)));

	Note(u"QPA mouse self-test: Test::Drag control: release animation, "
		u"observed and not judged: %1"_q.arg(
			AnimationText(log, state->controlStarted)));
	if (const auto parent = state->control.parent.data()) {
		parent->stopAnimation();
	}
	state->controlEnd = ReadEndState(*state);
	Check(
		state->controlAfterDrag.clean()
			&& state->controlEnd.clean()
			&& !AnyAnimating(*state),
		label + u": after Test::Drag's own leave, and again with the "
			u"control's animation stopped, Qt holds no button, no grab and "
			u"no hovered fixture widget, and no fixture animation runs"_q,
		u"afterDrag=[%1] afterStop=[%2] animating=%3; the probes create no "
		u"QDrag"_q.arg(
			EndStateText(state->controlAfterDrag),
			EndStateText(state->controlEnd),
			FlagText(AnyAnimating(*state))));
}

// One direct QWindowSystemInterface left-button event through |gesture|'s
// window, outside any QpaMouseSequence, inside Test::Settle only with
// |settle|. Before a press or a release Qt's last cursor position is put
// on its point, as the helper does, so Qt synthesizes no move before it;
// before a press Qt's double-click pairing button is reset too, unless
// |keepPairing|. False when the window is gone.
bool SendDirect(RawGesture &gesture, const DirectEvent &event) {
#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
	const auto window = gesture.window.data();
	if (!window) {
		return false;
	}
	const auto press = (event.type == QEvent::MouseButtonPress);
	const auto release = (event.type == QEvent::MouseButtonRelease);
	const auto button = (press || release) ? Qt::LeftButton : Qt::NoButton;
	const auto buttons = release
		? Qt::MouseButtons()
		: Qt::MouseButtons(Qt::LeftButton);
	gesture.sent = true;
	gesture.nativeLocal = event.nativeLocal;
	gesture.nativeGlobal = event.nativeGlobal;
	if (press || release) {
		gesture.held.setFlag(Qt::LeftButton, press);
	}
	const auto deliver = [&] {
		if (press && !event.keepPairing) {
			QGuiApplicationPrivate::mousePressButton = Qt::NoButton;
		}
		if (press || release) {
			QGuiApplicationPrivate::lastCursorPosition
				= QHighDpi::fromNativeGlobalPosition(
					event.nativeGlobal,
					window);
		}
		if (event.timestamp) {
			QWindowSystemInterface::handleMouseEvent<Delivery>(
				window,
				event.timestamp,
				event.nativeLocal,
				event.nativeGlobal,
				buttons,
				button,
				event.type,
				Qt::NoModifier);
		} else {
			QWindowSystemInterface::handleMouseEvent<Delivery>(
				window,
				event.nativeLocal,
				event.nativeGlobal,
				buttons,
				button,
				event.type,
				Qt::NoModifier);
		}
	};
	if (event.settle) {
		Settle(deliver);
	} else {
		deliver();
	}
	return true;
#else // !DESKTOP_APP_DISABLE_PRIVATE_QT_API
	return false;
#endif // DESKTOP_APP_DISABLE_PRIVATE_QT_API
}

// One direct delivery of the subject gesture at |point|, in the subject
// parent's coordinates, with the helper's mapping and pre-press
// preparation; inside Test::Settle only when |settle|. False when the
// parent or the window is gone.
bool RawDeliver(
		not_null<State*> state,
		bool settle,
		QEvent::Type type,
		QPoint point) {
#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
	const auto parent = state->subject.parent.data();
	const auto window = state->gesture.window.data();
	if (!parent || !window) {
		return false;
	}
	const auto local = parent->mapTo(parent->window(), QPointF(point));
	const auto global = parent->QWidget::mapToGlobal(QPointF(point));
	return SendDirect(state->gesture, {
		.nativeLocal = QHighDpi::toNativeLocalPosition(local, window),
		.nativeGlobal = QHighDpi::toNativeGlobalPosition(global, window),
		.type = type,
		.settle = settle,
	});
#else // !DESKTOP_APP_DISABLE_PRIVATE_QT_API
	return false;
#endif // DESKTOP_APP_DISABLE_PRIVATE_QT_API
}

// The explicit cleanup after a direct gesture: a release at its last point
// if it still holds the button, a fixture probe's grab if one is left, and
// a QPA leave, each inside Settle.
QString CleanupRaw(State &state) {
	auto &gesture = state.gesture;
	const auto held = gesture.held;
	const auto window = gesture.window;
	auto releaseSent = false;
	auto buttonsCleared = false;
	auto grabReleased = false;
	auto leaveSent = false;
	if ((held & Qt::LeftButton) && window) {
		releaseSent = SendDirect(gesture, {
			.nativeLocal = gesture.nativeLocal,
			.nativeGlobal = gesture.nativeGlobal,
			.type = QEvent::MouseButtonRelease,
			.settle = true,
		});
	}
#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
	const auto left = QGuiApplicationPrivate::mouse_buttons & held;
	if (left) {
		QGuiApplicationPrivate::mouse_buttons &= ~left;
		buttonsCleared = true;
	}
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API
	const auto grabber = QWidget::mouseGrabber();
	if (grabber && InFixture(state, grabber)) {
		grabber->releaseMouse();
		grabReleased = true;
	}
#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
	if (gesture.sent && window) {
		Settle([&] {
			QWindowSystemInterface::handleLeaveEvent<Delivery>(
				window.data());
		});
		leaveSent = true;
	}
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API
	gesture = RawGesture();
	return u"held=%1 releaseSent=%2 buttonsCleared=%3 grabReleased=%4 "
		u"leaveSent=%5"_q.arg(
			QString::number(held.toInt()),
			FlagText(releaseSent),
			FlagText(buttonsCleared),
			FlagText(grabReleased),
			FlagText(leaveSent));
}

void ReleaseIngredient(
		not_null<State*> state,
		not_null<IngredientLeg*> leg) {
	leg->releaseSentAt = crl::now();
	leg->releaseSent = RawDeliver(
		state,
		leg->settle,
		QEvent::MouseButtonRelease,
		state->to);
}

// The subject pair gets the leg's own fresh log; the press and the eight
// moves go at once, then the release at once, or - with |wait| - the
// queued-work markers are posted after the last move and the release
// waits for them.
void StartIngredient(not_null<State*> state, not_null<IngredientLeg*> leg) {
	leg->start = ReadEndState(*state);
	leg->animatingBefore = AnyAnimating(*state);
	leg->log = std::make_shared<ProbeLog>();
	SetPairLog(state->subject, leg->log);
	state->pairsWindow->events.clear();
	const auto top = state->pairs.get();
	const auto window = top ? top->windowHandle() : nullptr;
	if (!state->subject.parent || !window) {
		return;
	}
	state->gesture = RawGesture{ .window = window };
	const auto points = GesturePoints(state->from, state->to);
	leg->pressSent = RawDeliver(
		state,
		leg->settle,
		QEvent::MouseButtonPress,
		points.front());
	for (auto i = 1; i != int(points.size()); ++i) {
		if (RawDeliver(state, leg->settle, QEvent::MouseMove, points[i])) {
			++leg->movesSent;
		}
	}
	if (!leg->wait) {
		ReleaseIngredient(state, leg);
	} else if (const auto parent = state->subject.parent.data()) {
		leg->markers = PostQueuedWorkMarkers(parent);
	}
}

[[nodiscard]] bool IngredientWaited(const IngredientLeg &leg) {
	const auto markers = leg.markers.get();
	return !leg.wait
		|| !markers
		|| markers->ran()
		|| (crl::now() - markers->postedAt >= kQpaMouseDragBound);
}

void FinishIngredientWait(
		not_null<State*> state,
		not_null<IngredientLeg*> leg) {
	if (!leg->wait) {
		return;
	}
	if (const auto markers = leg->markers.get()) {
		leg->markersText = u"qtQueue=%1 mainQueue=%2 waitedMs=%3"_q.arg(
			FlagText(markers->qtQueueRan),
			FlagText(markers->mainQueueRan),
			QString::number(crl::now() - markers->postedAt));
	}
	ReleaseIngredient(state, leg);
}

[[nodiscard]] bool IngredientSettled(const IngredientLeg &leg) {
	return !leg.releaseSent
		|| !leg.log
		|| AnimationSettled(*leg.log, leg.releaseSentAt);
}

// The observation ends first; only then the animation is stopped, the
// gesture cleaned up and a drain posted behind everything the leg queued.
void ObserveIngredient(
		not_null<State*> state,
		not_null<IngredientLeg*> leg) {
	leg->window = state->pairsWindow->events;
	if (leg->log) {
		leg->route = *leg->log;
		leg->row = IngredientRow(
			leg->name,
			leg->settle,
			leg->wait,
			*leg->log,
			leg->releaseSentAt,
			leg->markersText);
		Note(leg->row);
	}
	if (const auto parent = state->subject.parent.data()) {
		parent->stopAnimation();
	}
	leg->cleanup = CleanupRaw(*state);
	if (const auto top = state->pairs.get()) {
		leg->drain = PostQueuedWorkMarkers(top);
	}
}

[[nodiscard]] bool IngredientDrained(const IngredientLeg &leg) {
	const auto drain = leg.drain.get();
	return !drain
		|| drain->ran()
		|| (crl::now() - drain->postedAt >= kQpaMouseDragBound);
}

// The leg's completion is never judged. What is: that it delivered the
// subject's exact route, so its reading compares with the helper's, and
// that it started and ended with no pointer state.
void CheckIngredient(not_null<State*> state, not_null<IngredientLeg*> leg) {
	SettlePostponedCalls();
	leg->end = ReadEndState(*state);
	const auto label = u"QPA mouse self-test: ingredient %1"_q.arg(
		leg->name);
	const auto received = Literals(leg->route.events);
	const auto documented = Literals(SubjectExpected(*state));
	const auto window = WindowLiterals(leg->window);
	Check(
		leg->pressSent
			&& (leg->movesSent == kSelfTestSteps)
			&& leg->releaseSent
			&& (received == documented)
			&& (window == ExpectedWindow()),
		label + u": its direct QWindowSystemInterface gesture took the "
			u"helper's exact route, so its animation reading compares with "
			u"the helper's"_q,
		u"pressSent=%1 movesSent=%2 releaseSent=%3 window=[%4] "
		u"documented=[%5] received=[%6]"_q.arg(
			FlagText(leg->pressSent),
			QString::number(leg->movesSent),
			FlagText(leg->releaseSent),
			window.join(u", "_q),
			documented.join(u"; "_q),
			received.join(u"; "_q)));
	const auto drained = leg->drain && leg->drain->ran();
	Check(
		leg->start.clean()
			&& !leg->animatingBefore
			&& drained
			&& leg->end.clean(),
		label + u": it started with no pointer state and no fixture "
			u"animation running, and after its explicit release, grab and "
			u"leave cleanup and a drain Qt holds no button, no grab and no "
			u"hovered fixture widget"_q,
		u"start=[%1] animatingBefore=%2 cleanup=[%3] drained=%4 end=[%5]"_q
			.arg(
				EndStateText(leg->start),
				FlagText(leg->animatingBefore),
				leg->cleanup,
				FlagText(drained),
				EndStateText(leg->end)));
}

void AppendIngredientLeg(
		not_null<Runner*> runner,
		const std::shared_ptr<State> &state,
		not_null<IngredientLeg*> leg) {
	const auto name = leg->name;
	runner->add({
		.name = u"QPA mouse self-test: ingredient %1 - direct QPA press, "
			u"moves and release"_q.arg(name),
		.skipReason = PlatformInputSkip,
		.run = [=] {
			StartIngredient(state.get(), leg);
		},
		.until = [=] {
			return IngredientWaited(*leg);
		},
		.then = [=] {
			FinishIngredientWait(state.get(), leg);
		},
		.timeout = kQpaMouseDragBound + kDefaultStageTimeout,
	});
	runner->add({
		.name = u"QPA mouse self-test: ingredient %1 - the release "
			u"animation until it ends or 2000 ms pass, not judged"_q.arg(name),
		.skipReason = PlatformInputSkip,
		.until = [=] {
			return IngredientSettled(*leg);
		},
		.then = [=] {
			ObserveIngredient(state.get(), leg);
		},
	});
	runner->add({
		.name = u"QPA mouse self-test: ingredient %1 - cleanup drained, end "
			u"state"_q.arg(name),
		.skipReason = PlatformInputSkip,
		.until = [=] {
			return IngredientDrained(*leg);
		},
		.then = [=] {
			CheckIngredient(state.get(), leg);
		},
	});
}

void NoteIngredients(const State &state) {
	Note(u"QPA_MOUSE_INGREDIENTS: both=[%1] raw=[%2] settle=[%3] "
		u"wait=[%4]"_q.arg(
			IngredientSummary(state.subjectLog.get()),
			IngredientSummary(state.raw.log.get()),
			IngredientSummary(state.settleOnly.log.get()),
			IngredientSummary(state.waitOnly.log.get())));
}

// The nearest and the farthest any mouse event in |log| lands from
// |expected|, per axis, by its local or its global position; -1 for both
// with no mouse event.
struct Spread {
	double nearest = -1.;
	double farthest = -1.;
};

[[nodiscard]] Spread SpreadFrom(
		const ProbeLog &log,
		QPointF expected,
		bool global) {
	auto result = Spread();
	for (const auto &event : MouseEvents(log.events)) {
		const auto delta = (global ? event.global : event.local) - expected;
		const auto distance = std::max(
			std::abs(delta.x()),
			std::abs(delta.y()));
		result.nearest = (result.nearest < 0.)
			? distance
			: std::min(result.nearest, distance);
		result.farthest = std::max(result.farthest, distance);
	}
	return result;
}

[[nodiscard]] QStringList MouseKinds(const ProbeLog &log) {
	auto result = QStringList();
	for (const auto &event : MouseEvents(log.events)) {
		result.push_back(KindName(event.type));
	}
	return result;
}

// "<kind> local=<x>,<y> global=<x>,<y>" per mouse event in |log|.
[[nodiscard]] QString LandedText(const ProbeLog &log) {
	auto result = QStringList();
	for (const auto &event : MouseEvents(log.events)) {
		result.push_back(u"%1 local=%2 global=%3"_q.arg(
			KindName(event.type),
			PointText(event.local),
			PointText(event.global)));
	}
	return result.join(u"; "_q);
}

[[nodiscard]] QString TimestampsText(const ProbeLog &log) {
	auto result = QStringList();
	for (const auto &event : MouseEvents(log.events)) {
		result.push_back(QString::number(event.timestamp));
	}
	return result.join(u","_q);
}

// One helper click at |point| through |sequence|: its press, its release,
// then its end().
QpaMouseEnd ClickThrough(QpaMouseSequence &sequence, QPoint point) {
	sequence.deliver({
		.type = QpaMouseType::Press,
		.point = point,
		.button = Qt::LeftButton,
		.buttons = Qt::LeftButton,
	});
	sequence.deliver({
		.type = QpaMouseType::Release,
		.point = point,
		.button = Qt::LeftButton,
	});
	return sequence.end();
}

// |point| of the pads window, in its logical coordinates, as the native
// window-local and global points QWindowSystemInterface takes: through
// Qt's conversion, or passed unconverted with |convert| false, as the
// coordinate leg's control does. Settled; the caller sets the type.
[[nodiscard]] DirectEvent PadEvent(
		const State &state,
		QPoint point,
		bool convert) {
	const auto top = state.pads.get();
	const auto window = top ? top->windowHandle() : nullptr;
	if (!window) {
		return {};
	}
	const auto local = QPointF(point);
	const auto global = top->QWidget::mapToGlobal(local);
	if (!convert) {
		return {
			.nativeLocal = local,
			.nativeGlobal = global,
			.settle = true,
		};
	}
#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
	return {
		.nativeLocal = QHighDpi::toNativeLocalPosition(local, window),
		.nativeGlobal = QHighDpi::toNativeGlobalPosition(global, window),
		.settle = true,
	};
#else // !DESKTOP_APP_DISABLE_PRIVATE_QT_API
	return {};
#endif // DESKTOP_APP_DISABLE_PRIVATE_QT_API
}

// P for the coordinate leg, in the pads window's coordinates: the middle
// of the part of |pad| whose points divided by |factor| - where an
// unconverted delivery lands - lie in |pad| too, both one pixel in from
// its edges. None when there is no such point.
[[nodiscard]] std::optional<QPoint> CoordinatePoint(
		QRect pad,
		double factor) {
	if (factor <= 0.) {
		return std::nullopt;
	}
	const auto inner = QRectF(pad).marginsRemoved(QMarginsF(1., 1., 1., 1.));
	const auto scaled = QRectF(
		inner.topLeft() * factor,
		inner.size() * factor);
	const auto both = inner.intersected(scaled);
	if (both.isEmpty()) {
		return std::nullopt;
	}
	const auto result = both.center().toPoint();
	if (!inner.contains(QPointF(result))
		|| !inner.contains(QPointF(result) / factor)) {
		return std::nullopt;
	}
	return result;
}

// The unconverted control first: the pads window's P passed as native
// points, pressed and released directly, then cleaned up. Then the
// helper's own click at P. Each gets its own pad log and end reading.
void RunCoordinates(not_null<State*> state) {
	auto &leg = state->coordinates;
	const auto pad = state->pad.data();
	const auto top = state->pads.get();
	const auto window = top ? top->windowHandle() : nullptr;
	if (!pad || !window) {
		leg.refusal = u"the landing pad or its window is gone"_q;
		return;
	}
	leg.dpr = window->devicePixelRatio();
	leg.factor = ConversionFactor(window);
	leg.origin = top->QWidget::mapToGlobal(QPointF());
	const auto point = CoordinatePoint(pad->geometry(), leg.factor);
	if (!point) {
		leg.refusal = u"no point P of the pad %1 lands inside it again "
			u"divided by factor %2"_q.arg(
				RectText(pad->geometry()),
				NumberText(leg.factor));
		return;
	}
	leg.point = *point;
	leg.padPoint = *point - pad->pos();

	const auto raw = std::make_shared<ProbeLog>();
	pad->setLog(raw);
	state->gesture = RawGesture{ .window = window };
	auto event = PadEvent(*state, leg.point, false);
	const auto types = std::array{
		QEvent::MouseButtonPress,
		QEvent::MouseButtonRelease,
	};
	for (const auto type : types) {
		event.type = type;
		SendDirect(state->gesture, event);
	}
	leg.rawCleanup = CleanupRaw(*state);
	leg.raw = *raw;
	leg.rawEnd = ReadEndState(*state);

	const auto converted = std::make_shared<ProbeLog>();
	pad->setLog(converted);
	auto sequence = QpaMouseSequence(pad);
	leg.end = ClickThrough(sequence, leg.padPoint);
	leg.converted = *converted;
	leg.convertedEnd = ReadEndState(*state);
}

// The helper's click must land on P, local and global; the unconverted
// control near P / factor. Only a factor other than 1 makes the control
// land away from P, so only that run discriminates: at factor 1 - every
// default macOS run, Retina devicePixelRatio 2 included - the control
// lands on P and the row says the run does not decide.
void CheckCoordinates(not_null<State*> state) {
	const auto &leg = state->coordinates;
	const auto label = u"QPA mouse self-test: coordinates"_q;
	const auto pad = state->pad.data();
	const auto padOrigin = pad ? QPointF(pad->pos()) : QPointF();
	const auto rawExpected = (leg.factor > 0.)
		? (QPointF(leg.point) / leg.factor - padOrigin)
		: QPointF();
	const auto raw = SpreadFrom(leg.raw, rawExpected, false);
	const auto rawFromP = SpreadFrom(leg.raw, QPointF(leg.padPoint), false);
	const auto local = SpreadFrom(
		leg.converted,
		QPointF(leg.padPoint),
		false);
	const auto global = SpreadFrom(
		leg.converted,
		leg.origin + QPointF(leg.point),
		true);
	const auto discriminating = leg.refusal.isEmpty()
		&& !qFuzzyCompare(leg.factor, 1.);
	const auto row = u"QPA_MOUSE_COORDINATES: dpr=%1 factor=%2 "
		u"discriminating=%3 P=%4 padP=%5 rawExpectedPadP=%6 rawError=%7 "
		u"rawFromP=%8 localError=%9 globalError=%10 raw=[%11] "
		u"helper=[%12] rawCleanup=[%13] rawEnd=[%14] end=[%15] "
		u"helperEnd=[%16]%17"_q.arg(
			NumberText(leg.dpr),
			NumberText(leg.factor),
			FlagText(discriminating),
			PointText(leg.point),
			PointText(leg.padPoint),
			PointText(rawExpected),
			NumberText(raw.farthest),
			NumberText(rawFromP.nearest),
			NumberText(local.farthest),
			NumberText(global.farthest),
			LandedText(leg.raw),
			LandedText(leg.converted),
			leg.rawCleanup,
			EndStateText(leg.rawEnd),
			QpaMouseEndText(leg.end),
			EndStateText(leg.convertedEnd),
			leg.refusal.isEmpty() ? QString() : (u" - "_q + leg.refusal));
	Note(row);
	const auto click = QStringList{ u"press"_q, u"release"_q };
	Check(
		leg.refusal.isEmpty()
			&& (MouseKinds(leg.raw) == click)
			&& (MouseKinds(leg.converted) == click)
			&& (local.farthest >= 0.)
			&& (local.farthest <= kPositionTolerance)
			&& (global.farthest >= 0.)
			&& (global.farthest <= kPositionTolerance)
			&& (raw.farthest >= 0.)
			&& (raw.farthest <= kNearTolerance)
			&& leg.rawEnd.clean()
			&& leg.end.clean()
			&& leg.convertedEnd.clean(),
		label + u": the helper's press and release reach the pad at P "
			u"within 0.01 px, local and global; the unconverted control's "
			u"reach it near P / factor; each ends with no pointer state"_q,
		row);
	if (!leg.refusal.isEmpty()) {
		return;
	} else if (discriminating) {
		Check(
			!qFuzzyCompare(leg.dpr, 1.)
				&& (rawFromP.nearest > kNearTolerance),
			label + u": at a conversion factor other than 1 the unconverted "
				u"control lands more than 0.5 px from P, where the helper "
				u"lands: the conversion is exercised"_q,
			row);
	} else {
		Check(
			(rawFromP.farthest >= 0.)
				&& (rawFromP.farthest <= kPositionTolerance),
			label + u": at conversion factor 1 the unconverted control "
				u"lands on P too, whatever the devicePixelRatio: identity, "
				u"so this run does not discriminate and a QT_SCALE_FACTOR "
				u"run decides"_q,
			row);
	}
}

// The first of the two helper clicks at the pad's centre; the stage's
// then, a runner tick later and inside Qt's double-click interval, makes
// the second.
void RunFirstClick(not_null<State*> state) {
	auto &leg = state->clicks;
	const auto pad = state->pad.data();
	if (!pad) {
		return;
	}
	leg.point = pad->rect().center();
	leg.facility = std::make_shared<ProbeLog>();
	pad->setLog(leg.facility);
	state->padsWindow->events.clear();
	auto sequence = QpaMouseSequence(pad);
	leg.firstEnd = ClickThrough(sequence, leg.point);
}

// The second helper click through its own sequence, then that completed
// sequence's end() once more, as a finish callback would call it.
void RunSecondClick(not_null<State*> state) {
	auto &leg = state->clicks;
	const auto pad = state->pad.data();
	if (!pad || !leg.facility) {
		return;
	}
	auto sequence = QpaMouseSequence(pad);
	leg.secondEnd = ClickThrough(sequence, leg.point);
	leg.facilityWindow = state->padsWindow->events;
	leg.facilityEnd = ReadEndState(*state);
	const auto events = int(leg.facility->events.size());
	const auto windowEvents = int(state->padsWindow->events.size());
	leg.repeated = sequence.end();
	leg.repeatedEvents = int(leg.facility->events.size()) - events;
	leg.repeatedWindowEvents = int(state->padsWindow->events.size())
		- windowEvents;
}

void CheckFacilityClicks(not_null<State*> state) {
	const auto &leg = state->clicks;
	const auto label = u"QPA mouse self-test: double click"_q;
	const auto log = leg.facility ? *leg.facility : ProbeLog();
	const auto events = MouseEvents(log.events);
	const auto interval = qint64(
		QGuiApplication::styleHints()->mouseDoubleClickInterval());
	const auto delta = (events.size() == 4)
		? (qint64(events[2].timestamp) - qint64(events[0].timestamp))
		: qint64(-1);
	const auto pad = MouseKinds(log);
	const auto window = WindowLiterals(leg.facilityWindow);
	Check(
		(pad == QStringList{
			u"press"_q,
			u"release"_q,
			u"press"_q,
			u"release"_q,
		})
			&& (window == QStringList{
				u"press:1"_q,
				u"release:1"_q,
				u"press:1"_q,
				u"release:1"_q,
			})
			&& (delta > 0)
			&& (delta < interval),
		label + u": two helper clicks at one pad point, their presses "
			u"apart by less than Qt's double-click interval, reach the pad "
			u"and its window as exactly press, release, press, release"_q,
		u"pad=[%1] window=[%2] pressDeltaMs=%3 intervalMs=%4 "
		u"timestamps=[%5]"_q.arg(
			pad.join(u", "_q),
			window.join(u", "_q),
			QString::number(delta),
			QString::number(interval),
			TimestampsText(log)));
	Check(
		leg.firstEnd.clean()
			&& leg.secondEnd.clean()
			&& leg.facilityEnd.clean(),
		label + u": after each helper click's end() Qt holds no button, no "
			u"grab and no hovered fixture widget"_q,
		u"first=[%1] second=[%2] fixture=[%3]"_q.arg(
			QpaMouseEndText(leg.firstEnd),
			QpaMouseEndText(leg.secondEnd),
			EndStateText(leg.facilityEnd)));
	Check(
		(QpaMouseEndText(leg.repeated) == QpaMouseEndText(leg.secondEnd))
			&& !leg.repeatedEvents
			&& !leg.repeatedWindowEvents,
		label + u": end() called again on the completed sequence, as a "
			u"finish callback does, returns the same reading and delivers "
			u"nothing"_q,
		u"repeated=[%1] addedPadEvents=%2 addedWindowEvents=%3"_q.arg(
			QpaMouseEndText(leg.repeated),
			QString::number(leg.repeatedEvents),
			QString::number(leg.repeatedWindowEvents)));
}

// The positive control: two direct clicks at the same point, stamped
// base + 1 to base + 4 from the last timestamp the helper's clicks left,
// Qt's pairing button reset before the first press only.
void RunRawClicks(not_null<State*> state) {
	auto &leg = state->clicks;
	const auto pad = state->pad.data();
	const auto top = state->pads.get();
	const auto window = top ? top->windowHandle() : nullptr;
	const auto events = leg.facility
		? MouseEvents(leg.facility->events)
		: std::vector<ProbeEvent>();
	if (!pad || !window || events.empty()) {
		return;
	}
	leg.base = events.back().timestamp;
	leg.raw = std::make_shared<ProbeLog>();
	pad->setLog(leg.raw);
	state->padsWindow->events.clear();
	state->gesture = RawGesture{ .window = window };
	auto event = PadEvent(*state, pad->mapTo(top, leg.point), true);
	const auto types = std::array{
		QEvent::MouseButtonPress,
		QEvent::MouseButtonRelease,
		QEvent::MouseButtonPress,
		QEvent::MouseButtonRelease,
	};
	for (auto i = 0; i != int(types.size()); ++i) {
		event.type = types[i];
		event.timestamp = ulong(leg.base) + ulong(i) + 1;
		event.keepPairing = (i > 0);
		SendDirect(state->gesture, event);
	}
	leg.rawWindow = state->padsWindow->events;
	leg.rawCleanup = CleanupRaw(*state);
	leg.rawEnd = ReadEndState(*state);
}

// Qt marks the second press as a double click: QWidgetWindow drops that
// press for the widget (QTBUG-25831) and the pad gets the double click
// instead, while the window gets both.
void CheckRawClicks(not_null<State*> state) {
	const auto &leg = state->clicks;
	const auto label = u"QPA mouse self-test: double click control"_q;
	const auto log = leg.raw ? *leg.raw : ProbeLog();
	const auto events = MouseEvents(log.events);
	const auto pad = MouseKinds(log);
	const auto window = WindowLiterals(leg.rawWindow);
	const auto stamped = (events.size() == 4)
		&& (events[0].timestamp == leg.base + 1)
		&& (events[2].timestamp == leg.base + 3);
	Check(
		(pad == QStringList{
			u"press"_q,
			u"release"_q,
			u"double"_q,
			u"release"_q,
		})
			&& (window == QStringList{
				u"press:1"_q,
				u"release:1"_q,
				u"press:1"_q,
				u"double:1"_q,
				u"release:1"_q,
			})
			&& stamped
			&& leg.rawEnd.clean(),
		label + u": two direct clicks at the same point stamped 2 ms apart "
			u"make exactly one double click: the pad gets press, release, "
			u"double, release, its window the marked second press too, and "
			u"no pointer state remains"_q,
		u"pad=[%1] window=[%2] base=%3 timestamps=[%4] cleanup=[%5] "
		u"end=[%6]"_q.arg(
			pad.join(u", "_q),
			window.join(u", "_q),
			QString::number(leg.base),
			TimestampsText(log),
			leg.rawCleanup,
			EndStateText(leg.rawEnd)));
}

// Arms the self-test's own console shield and gives the subject pair a
// fresh log for the paced gesture.
void ArmForSubject(not_null<Runner*> runner, not_null<State*> state) {
	auto &leg = state->armed;
	ArmConsoleShield(runner, kShieldLabel);
	leg.before = ReadConsoleShield();
	leg.log = std::make_shared<ProbeLog>();
	SetPairLog(state->subject, leg.log);
	state->pairsWindow->events.clear();
}

[[nodiscard]] QString ShieldText(const ConsoleShieldReading &reading) {
	return u"armedAt=%1 %2"_q.arg(
		QString::number(reading.armedAt),
		ConsoleShieldSummary(reading));
}

// Right after the paced drag's then: the unarmed subject's checks on the
// same probes, the shield around the gesture, then the owned release
// before any window closes.
void CheckArmedSubject(not_null<State*> state) {
	auto &leg = state->armed;
	const auto label = u"QPA mouse self-test: armed console shield"_q;
	leg.after = ReadConsoleShield();
	leg.end = ReadEndState(*state);
	leg.route = leg.log ? *leg.log : ProbeLog();
	leg.window = state->pairsWindow->events;
	const auto &drag = *leg.drag;
	CheckSubjectRoute(*state, label, leg.route, leg.window, drag, leg.end);
	Check(
		(drag.paceMs == kSelfTestPace)
			&& (drag.steps == kSelfTestSteps)
			&& (drag.moves == kSelfTestSteps),
		label + u": paced at one move per 50 ms, all eight requested held "
			u"moves were delivered"_q,
		QpaMouseDragText(drag));
	const auto &before = leg.before;
	const auto &after = leg.after;
	Check(
		before.armed
			&& after.armed
			&& (before.label == kShieldLabel)
			&& (after.label == kShieldLabel)
			&& (after.armedAt == before.armedAt)
			&& (after.vetoes == before.vetoes)
			&& (after.counts == before.counts),
		label + u": the self-test's own arm held through the whole gesture "
			u"and counted no veto: the helper's spontaneous QPA events pass "
			u"as harness input"_q,
		u"before: %1 after: %2"_q.arg(ShieldText(before), ShieldText(after)));
	if (const auto parent = state->subject.parent.data()) {
		parent->stopAnimation();
	}
	leg.releasedOwned = ReleaseOwnedShield(u"qpa-mouse-self-test"_q);
	leg.released = ReadConsoleShield();
	Check(
		leg.releasedOwned
			&& !leg.released.armed
			&& (leg.released.vetoes == after.vetoes),
		label + u": the self-test releases its own arm before any of its "
			u"windows closes"_q,
		u"released=%1 %2"_q.arg(
			FlagText(leg.releasedOwned),
			ShieldText(leg.released)));
}

// Replaces the pair in the control row - the control pair once its legs
// are done, or an earlier spare - with a fresh pair of |mode|, so the
// legs reuse one row rather than need a taller window.
void RebuildSpare(
		not_null<State*> state,
		ParentMode mode,
		std::shared_ptr<ProbeLog> log) {
	delete state->control.parent.data();
	delete state->spare.parent.data();
	state->spare = ProbePair();
	if (const auto top = state->pairs.get()) {
		BuildPair(
			state->spare,
			top,
			state->spareOrigin,
			state->cell,
			std::move(log),
			mode);
	}
}

[[nodiscard]] QpaMouseDragTarget SpareTarget(const State &state) {
	return {
		.widget = state.spare.parent.data(),
		.from = state.from,
		.to = state.to,
	};
}

// The parent's press and moves as milliseconds after the press, and its
// deletion, with the pair's order.
[[nodiscard]] QString PacingText(const ProbeLog &log) {
	auto pressedAt = crl::time(0);
	auto moves = QStringList();
	for (const auto &event : MouseEvents(log.events, u"parent"_q)) {
		if (event.type == QEvent::MouseButtonPress) {
			pressedAt = event.at;
		} else if (event.type == QEvent::MouseMove) {
			moves.push_back(QString::number(event.at - pressedAt));
		}
	}
	return u"movesAfterPressMs=[%1] destroyedAfterPressMs=%2 order=[%3]"_q
		.arg(
			moves.join(u","_q),
			(log.destroyedAt
				? QString::number(log.destroyedAt - pressedAt)
				: u"none"_q),
			log.order.join(u", "_q));
}

// The probe queued its own deletion at held move 2. "turn:1" between the
// two moves shows that they came in separate turns, and "destroyed" after
// the second with no probe event behind it shows that the deletion ran
// before any further paced move.
void CheckDestroyed(not_null<State*> state) {
	const auto label = u"QPA mouse self-test: destroyed target"_q;
	const auto &drag = *state->destroyedDrag;
	const auto log = state->destroyedLog
		? *state->destroyedLog
		: ProbeLog();
	const auto end = ReadEndState(*state);
	const auto destroyed = int(log.order.indexOf(u"destroyed"_q));
	const auto turn = int(log.order.indexOf(u"turn:1"_q));
	auto moves = std::vector<int>();
	auto later = 0;
	for (auto i = 0; i != int(log.order.size()); ++i) {
		const auto &entry = log.order[i];
		if (entry == u"parent:move"_q) {
			moves.push_back(i);
		}
		if ((destroyed >= 0)
			&& (i > destroyed)
			&& (entry.startsWith(u"parent:"_q)
				|| entry.startsWith(u"child:"_q))) {
			++later;
		}
	}
	const auto releases = CountEvents(
		log,
		u"parent"_q,
		QEvent::MouseButtonRelease)
		+ CountEvents(log, u"child"_q, QEvent::MouseButtonRelease);
	const auto ordered = (moves.size() == 2)
		&& (moves[0] < turn)
		&& (turn < moves[1])
		&& (moves[1] < destroyed);
	Check(
		!state->spare.parent
			&& ordered
			&& !later
			&& !releases
			&& !CountEvents(log, u"child"_q, QEvent::MouseMove),
		label + u": paced at 50 ms, the probe queued its own deletion at "
			u"held move 2; move 1's turn ended before move 2, the deletion "
			u"ran before any further move, and the probe got exactly two "
			u"moves, no release and nothing after its deletion"_q,
		u"paceMs=%1 %2"_q.arg(
			QString::number(kSelfTestPace),
			PacingText(log)));
	Check(
		drag.finished
			&& !drag.aborted
			&& (drag.refusal == QpaMouseRefusal::TargetDestroyed)
			&& drag.pressed
			&& (drag.moves == kPostAtMove)
			&& (drag.moves < drag.steps)
			&& !drag.released
			&& drag.end.ended
			&& drag.end.releaseSent
			&& !drag.end.targetAlive
			&& drag.end.clean()
			&& end.clean(),
		label + u": the helper names target-destroyed after two of the "
			u"eight requested moves and sends the gesture's release to no "
			u"probe; end() releases the held button through the live window "
			u"and leaves no button, grab or hovered fixture widget"_q,
		u"%1 fixture=[%2]"_q.arg(QpaMouseDragText(drag), EndStateText(end)));
}

// Every queued call to the probe was withheld: the Qt marker never ran,
// the main-queue one did, and the bound expired with the release still
// sent through the probe's grab.
void CheckExpiry(not_null<State*> state) {
	const auto label = u"QPA mouse self-test: bound expired"_q;
	const auto &drag = *state->expiryDrag;
	const auto log = state->expiryLog ? *state->expiryLog : ProbeLog();
	const auto end = ReadEndState(*state);
	const auto releases = CountEvents(
		log,
		u"parent"_q,
		QEvent::MouseButtonRelease);
	Check(
		drag.finished
			&& !drag.aborted
			&& (drag.refusal == QpaMouseRefusal::BoundExpired)
			&& !drag.qtQueueRan
			&& drag.mainQueueRan
			&& (drag.bound == kExpiryBound)
			&& (drag.waitedMs >= kExpiryBound)
			&& drag.pressed
			&& (drag.moves == kSelfTestSteps)
			&& drag.released
			&& (releases == 1)
			&& (log.swallowed > 0),
		label + u": with every queued call to the probe withheld, the Qt "
			u"marker never ran and the main-queue one did; once 300 ms "
			u"passed the helper names bound-expired and still sends exactly "
			u"one release, which the probe gets through its grab"_q,
		u"swallowed=%1 parentReleases=%2 %3"_q.arg(
			QString::number(log.swallowed),
			QString::number(releases),
			QpaMouseDragText(drag)));
	Check(
		drag.end.ended
			&& !drag.end.releaseSent
			&& drag.end.clean()
			&& end.clean(),
		label + u": after it Qt holds no button, no grab and no hovered "
			u"fixture widget"_q,
		u"end=[%1] fixture=[%2]"_q.arg(
			QpaMouseEndText(drag.end),
			EndStateText(end)));
	if (const auto parent = state->spare.parent.data()) {
		parent->stopAnimation();
	}
}

// A never-shown parentless window and one shown with
// Qt::WA_DontShowOnScreen, each with a recording LandingPad.
void BuildRefusalTargets(not_null<State*> state) {
	auto &refused = state->refused;
	const auto size = QSize(4 * state->cell, 2 * state->cell);
	const auto addPad = [&](
			not_null<QWidget*> top,
			std::shared_ptr<ProbeLog> log) {
		const auto pad = Ui::CreateChild<LandingPad>(top.get());
		pad->setGeometry(QRect(QPoint(), size));
		pad->setLog(std::move(log));
		pad->show();
		return QPointer<LandingPad>(pad);
	};
	refused.unshownLog = std::make_shared<ProbeLog>();
	refused.offscreenLog = std::make_shared<ProbeLog>();
	refused.unshown = MakeWindow(u"unshown"_q, size);
	refused.unshownPad = addPad(refused.unshown.get(), refused.unshownLog);
	refused.unshownHandleBefore
		= (refused.unshown->windowHandle() != nullptr);
	refused.offscreen = MakeWindow(u"offscreen"_q, size);
	refused.offscreen->setAttribute(Qt::WA_DontShowOnScreen);
	refused.offscreenPad = addPad(
		refused.offscreen.get(),
		refused.offscreenLog);
	refused.offscreen->show();
}

[[nodiscard]] QpaMouseDragTarget PadTarget(LandingPad *pad, int cell) {
	const auto center = pad ? pad->rect().center() : QPoint();
	return {
		.widget = pad,
		.from = center,
		.to = center + QPoint(cell, 0),
	};
}

// A drag refused before its press: the named refusal, nothing sent, the
// target's recorder unchanged, an end() with nothing to undo, and the
// target's own condition |targetOk|, described by |targetText|.
void CheckRefused(
		const QString &label,
		const QpaMouseDrag &drag,
		QpaMouseRefusal refusal,
		const std::shared_ptr<ProbeLog> &log,
		bool targetOk,
		const QString &targetText,
		const EndState &end) {
	const auto received = log ? int(log->events.size()) : -1;
	Check(
		drag.finished
			&& !drag.aborted
			&& (drag.refusal == refusal)
			&& !drag.pressed
			&& !drag.moves
			&& !drag.released
			&& drag.end.ended
			&& !drag.end.releaseSent
			&& !drag.end.buttonsCleared
			&& !drag.end.grabReleased
			&& !drag.end.leaveSent
			&& drag.end.clean()
			&& !received
			&& targetOk
			&& end.clean(),
		label + u": the helper refuses %1 before its press: nothing is "
			u"sent, the target receives nothing, end() has nothing to undo "
			u"and Qt holds no pointer state"_q.arg(
				QpaMouseRefusalName(refusal)),
		u"%1 received=%2 %3 fixture=[%4]"_q.arg(
			targetText,
			QString::number(received),
			QpaMouseDragText(drag),
			EndStateText(end)));
}

void CheckRefusals(not_null<State*> state) {
	const auto &refused = state->refused;
	const auto end = ReadEndState(*state);
	const auto unshown = refused.unshown.get();
	const auto unshownHandle = unshown && unshown->windowHandle();
	CheckRefused(
		u"QPA mouse self-test: never-shown window"_q,
		*refused.unshownDrag,
		QpaMouseRefusal::NoWindowHandle,
		refused.unshownLog,
		unshown && !refused.unshownHandleBefore && !unshownHandle,
		u"handleBefore=%1 handleAfter=%2"_q.arg(
			FlagText(refused.unshownHandleBefore),
			FlagText(unshownHandle)),
		end);
	const auto offscreen = refused.offscreen.get();
	const auto handle = offscreen ? offscreen->windowHandle() : nullptr;
	const auto dontShow = offscreen
		&& offscreen->testAttribute(Qt::WA_DontShowOnScreen);
	CheckRefused(
		u"QPA mouse self-test: Qt::WA_DontShowOnScreen window"_q,
		*refused.offscreenDrag,
		QpaMouseRefusal::WindowHidden,
		refused.offscreenLog,
		offscreen && offscreen->isVisible() && handle && dontShow,
		u"shown=%1 handle=%2 handleVisible=%3 dontShowOnScreen=%4"_q.arg(
			FlagText(offscreen && offscreen->isVisible()),
			FlagText(handle != nullptr),
			FlagText(handle && handle->isVisible()),
			FlagText(dontShow)),
		end);
}

// Without the private API a delivery and the subject's drag are refused
// unsupported, sending nothing; the dependent legs are skipped by name.
void CheckUnsupported(not_null<State*> state) {
	auto sequence = QpaMouseSequence(state->pad.data());
	const auto press = sequence.deliver({
		.type = QpaMouseType::Press,
		.button = Qt::LeftButton,
		.buttons = Qt::LeftButton,
	});
	const auto end = sequence.end();
	const auto &drag = *state->subjectDrag;
	Check(
		(press.refusal == QpaMouseRefusal::Unsupported)
			&& !press.sent
			&& !press.delivered
			&& end.ended
			&& !end.releaseSent
			&& !end.leaveSent
			&& drag.finished
			&& (drag.refusal == QpaMouseRefusal::Unsupported)
			&& !drag.pressed
			&& !drag.moves,
		u"QPA mouse self-test: unsupported: built with "
		u"DESKTOP_APP_DISABLE_PRIVATE_QT_API, a delivery and the subject's "
		u"drag are refused unsupported and send nothing"_q,
		u"%1; %2"_q.arg(
			QpaMouseDeliveryText(press),
			QpaMouseDragText(drag)));
}

void AppendRouteLegs(
		not_null<Runner*> runner,
		const std::shared_ptr<State> &state) {
	state->subjectDrag = AppendQpaMouseDrag(runner, {
		.name = u"self-test subject"_q,
		.resolve = [=] {
			return SubjectTarget(*state);
		},
	});
	runner->add({
		.name = u"QPA mouse self-test: subject - route, sequence, queue "
			u"order and end state, then the release animation"_q,
		.skipReason = PlatformInputSkip,
		.run = [=] {
			ReadSubjectLeg(state.get());
		},
		.until = [=] {
			return AnimationSettled(
				*state->subjectLog,
				state->subjectObservedFrom);
		},
		.then = [=] {
			CheckSubjectAnimation(state.get());
		},
	});
	runner->add({
		.name = u"QPA mouse self-test: Test::Drag control - the same points "
			u"on an identical child"_q,
		.run = [=] {
			RunControl(state.get());
		},
		.until = [=] {
			return ControlSettled(*state);
		},
		.then = [=] {
			CheckControl(state.get());
		},
	});
	AppendIngredientLeg(runner, state, &state->raw);
	AppendIngredientLeg(runner, state, &state->settleOnly);
	AppendIngredientLeg(runner, state, &state->waitOnly);
	runner->add({
		.name = u"QPA mouse self-test: Run 1's ingredients side by side, "
			u"not judged"_q,
		.skipReason = PlatformInputSkip,
		.run = [=] {
			NoteIngredients(*state);
		},
	});
}

void AppendCoordinateLeg(
		not_null<Runner*> runner,
		const std::shared_ptr<State> &state) {
	runner->add({
		.name = u"QPA mouse self-test: coordinates - an unconverted direct "
			u"click at P on the pad, then the helper's"_q,
		.skipReason = PlatformInputSkip,
		.run = [=] {
			RunCoordinates(state.get());
			CheckCoordinates(state.get());
		},
	});
}

void AppendClickLegs(
		not_null<Runner*> runner,
		const std::shared_ptr<State> &state) {
	runner->add({
		.name = u"QPA mouse self-test: double click - two helper clicks at "
			u"one pad point, a runner tick apart"_q,
		.skipReason = PlatformInputSkip,
		.run = [=] {
			RunFirstClick(state.get());
		},
		.then = [=] {
			RunSecondClick(state.get());
			CheckFacilityClicks(state.get());
		},
	});
	runner->add({
		.name = u"QPA mouse self-test: double click - the positive control, "
			u"two direct clicks stamped inside the interval"_q,
		.skipReason = PlatformInputSkip,
		.run = [=] {
			RunRawClicks(state.get());
			CheckRawClicks(state.get());
		},
	});
}

void AppendArmedLeg(
		not_null<Runner*> runner,
		const std::shared_ptr<State> &state) {
	runner->add({
		.name = u"QPA mouse self-test: armed console shield - the "
			u"self-test's own arm"_q,
		.skipReason = PlatformInputSkip,
		.run = [=] {
			ArmForSubject(runner, state.get());
		},
	});
	state->armed.drag = AppendQpaMouseDrag(runner, {
		.name = u"self-test armed subject"_q,
		.resolve = [=] {
			return SubjectTarget(*state);
		},
		.skipReason = PlatformInputSkip,
		.paceMs = kSelfTestPace,
	});
	runner->add({
		.name = u"QPA mouse self-test: armed console shield - the same "
			u"route with no veto, then the owned release"_q,
		.skipReason = PlatformInputSkip,
		.run = [=] {
			CheckArmedSubject(state.get());
		},
	});
}

void AppendDestroyedLeg(
		not_null<Runner*> runner,
		const std::shared_ptr<State> &state) {
	runner->add({
		.name = u"QPA mouse self-test: destroyed target - a fresh pair in "
			u"the control row that deletes itself at held move 2"_q,
		.skipReason = PlatformInputSkip,
		.run = [=] {
			state->destroyedLog = std::make_shared<ProbeLog>();
			RebuildSpare(
				state.get(),
				ParentMode::DeleteAtMove,
				state->destroyedLog);
		},
	});
	state->destroyedDrag = AppendQpaMouseDrag(runner, {
		.name = u"self-test destroyed target"_q,
		.resolve = [=] {
			return SpareTarget(*state);
		},
		.skipReason = PlatformInputSkip,
		.paceMs = kSelfTestPace,
	});
	runner->add({
		.name = u"QPA mouse self-test: destroyed target - refusal, moves, "
			u"order and end state"_q,
		.skipReason = PlatformInputSkip,
		.run = [=] {
			CheckDestroyed(state.get());
		},
	});
}

void AppendExpiryLeg(
		not_null<Runner*> runner,
		const std::shared_ptr<State> &state) {
	runner->add({
		.name = u"QPA mouse self-test: bound expired - a fresh pair in the "
			u"control row that withholds every queued call"_q,
		.skipReason = PlatformInputSkip,
		.run = [=] {
			state->expiryLog = std::make_shared<ProbeLog>();
			RebuildSpare(
				state.get(),
				ParentMode::SwallowQueuedCalls,
				state->expiryLog);
		},
	});
	state->expiryDrag = AppendQpaMouseDrag(runner, {
		.name = u"self-test bound expired"_q,
		.resolve = [=] {
			return SpareTarget(*state);
		},
		.skipReason = PlatformInputSkip,
		.bound = kExpiryBound,
	});
	runner->add({
		.name = u"QPA mouse self-test: bound expired - refusal, release and "
			u"end state"_q,
		.skipReason = PlatformInputSkip,
		.run = [=] {
			CheckExpiry(state.get());
		},
	});
}

void AppendRefusalLegs(
		not_null<Runner*> runner,
		const std::shared_ptr<State> &state) {
	runner->add({
		.name = u"QPA mouse self-test: refusals - a never-shown window and "
			u"a shown Qt::WA_DontShowOnScreen one, each with a recording "
			u"pad"_q,
		.skipReason = PlatformInputSkip,
		.run = [=] {
			BuildRefusalTargets(state.get());
		},
	});
	state->refused.unshownDrag = AppendQpaMouseDrag(runner, {
		.name = u"self-test never-shown window"_q,
		.resolve = [=] {
			return PadTarget(state->refused.unshownPad.data(), state->cell);
		},
		.skipReason = PlatformInputSkip,
	});
	state->refused.offscreenDrag = AppendQpaMouseDrag(runner, {
		.name = u"self-test offscreen window"_q,
		.resolve = [=] {
			return PadTarget(state->refused.offscreenPad.data(), state->cell);
		},
		.skipReason = PlatformInputSkip,
	});
	runner->add({
		.name = u"QPA mouse self-test: refusals - named, with nothing "
			u"delivered"_q,
		.skipReason = PlatformInputSkip,
		.run = [=] {
			CheckRefusals(state.get());
		},
	});
}

// Only a build without the private API checks the unsupported refusal; a
// Qt-enabled run adds no stage for it, so it reports no N/A either.
void AppendUnsupportedCheck(
		not_null<Runner*> runner,
		const std::shared_ptr<State> &state) {
	if constexpr (!kPlatformInput) {
		runner->add({
			.name = u"QPA mouse self-test: unsupported - a delivery and the "
				u"subject's drag are refused by name"_q,
			.run = [=] {
				CheckUnsupported(state.get());
			},
		});
	}
}

} // namespace

void AppendQpaMouseSelfTest(not_null<Runner*> runner) {
	const auto state = std::make_shared<State>();
	state->raw.name = u"raw"_q;
	state->settleOnly.name = u"settle"_q;
	state->settleOnly.settle = true;
	state->waitOnly.name = u"wait"_q;
	state->waitOnly.wait = true;
	const auto gate = [=] {
		return state->gate;
	};

	runner->add({
		.name = u"QPA mouse self-test: fixture - probe pairs and a landing "
			u"pad in two ordinary windows shown without activation"_q,
		.run = [=] {
			state->buildRefusal = BuildFixture(state.get());
		},
		.until = [=] {
			return !state->buildRefusal.isEmpty()
				|| (WindowReady(state->pairs.get())
					&& WindowReady(state->pads.get()));
		},
		.then = [=] {
			FinishFixture(state.get());
		},
		.timeoutDetails = [=] {
			return FixtureText(*state);
		},
	});

	runner->addGated(gate, [&] {
		AppendRouteLegs(runner, state);
		AppendCoordinateLeg(runner, state);
		AppendClickLegs(runner, state);
		AppendArmedLeg(runner, state);
		AppendDestroyedLeg(runner, state);
		AppendExpiryLeg(runner, state);
		AppendRefusalLegs(runner, state);
		AppendUnsupportedCheck(runner, state);
	});

	runner->addRecovery({
		.name = u"QPA mouse self-test: teardown - release an owned shield "
			u"arm, end a direct gesture left open, then destroy the probe "
			u"windows"_q,
		.run = [=] {
			const auto released = ReleaseOwnedShield(
				u"qpa-mouse-self-test-teardown"_q);
			const auto cleanup = CleanupRaw(*state);
			const auto windows = FixtureWindows(*state);
			DestroyFixture(*state);
			Note(u"QPA mouse self-test: teardown shieldReleased=%1 %2 "
				u"windows=%3->%4"_q.arg(
					FlagText(released),
					cleanup,
					QString::number(windows),
					QString::number(FixtureWindows(*state))));
		},
	});

	// Registered after every AppendQpaMouseDrag above, so each drag's own
	// finish callback stops its pacing and ends its sequence before the
	// windows go; an arm the self-test still owns is released before its
	// windows close, and no other arm is touched.
	runner->onFinish([=] {
		ReleaseOwnedShield(u"qpa-mouse-self-test-finish"_q);
		CleanupRaw(*state);
		DestroyFixture(*state);
	});
}

} // namespace Test

#endif // _DEBUG
