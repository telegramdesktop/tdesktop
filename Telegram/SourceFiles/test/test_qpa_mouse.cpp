/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_qpa_mouse.h"

#include "base/algorithm.h"
#include "base/timer.h"
#include "test/test_capture.h"
#include "test/test_log.h"
#include "test/test_queued_drag.h"
#include "test/test_runner.h"
#include "test/test_widgets.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QEvent>
#include <QtCore/QLocale>
#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

#include <algorithm>
#include <bit>

#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
#include <private/qguiapplication_p.h>
#include <private/qhighdpiscaling_p.h>
#include <private/qwindow_p.h>
#include <qpa/qwindowsysteminterface.h>
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API

namespace Test {
namespace {

#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
using Delivery = QWindowSystemInterface::SynchronousDelivery;

// Watches one synchronous delivery as an application event filter, which
// Qt puts in front of every earlier one, for the call's duration only. It
// never vetoes. It describes the first widget receiver while Qt is still
// delivering to it and keeps no pointer to it, since a handler may delete
// that widget.
class DeliveryRecorder final : public QObject {
public:
	DeliveryRecorder(not_null<QWindow*> window, QEvent::Type type);
	~DeliveryRecorder();

	void read(QpaMouseDelivery &reading) const;

protected:
	bool eventFilter(QObject *receiver, QEvent *event) override;

private:
	const QPointer<QWindow> _window;
	const QEvent::Type _type = QEvent::None;
	QString _receiver;
	int _windowEvents = 0;
	bool _delivered = false;
	bool _spontaneous = false;
	bool _receiverSeen = false;
	bool _receiverSpontaneous = false;

};

// The target's own top-level and its QWindow, as they exist: no QWindow is
// created here. A refusal carries its reason.
struct ResolvedWindow {
	QWidget *top = nullptr;
	QWindow *window = nullptr;
	QString reason;
	QpaMouseRefusal refusal = QpaMouseRefusal::None;
};
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API

// Everything one appended drag stage owns. The pace timer's callback holds
// a raw pointer to this, which the timer cannot outlive. |closed| is set
// when |then| or the finish callback takes the reading; nothing writes it
// after that.
struct QpaMouseDragDriver {
	QpaMouseDragArgs args;
	std::shared_ptr<QpaMouseDrag> reading;
	std::unique_ptr<QpaMouseSequence> sequence;
	std::shared_ptr<const QueuedWorkMarkers> markers;
	QPointer<QWidget> target;
	base::Timer pace;
	int step = 0;
	bool stopped = false;
	bool closed = false;
};

#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
DeliveryRecorder::DeliveryRecorder(
	not_null<QWindow*> window,
	QEvent::Type type)
: _window(window.get())
, _type(type) {
	QCoreApplication::instance()->installEventFilter(this);
}

DeliveryRecorder::~DeliveryRecorder() {
	QCoreApplication::instance()->removeEventFilter(this);
}

void DeliveryRecorder::read(QpaMouseDelivery &reading) const {
	reading.delivered = _delivered;
	reading.spontaneous = _spontaneous;
	reading.windowEvents = _windowEvents;
	reading.receiver = _receiver;
	reading.receiverSpontaneous = _receiverSpontaneous;
}

bool DeliveryRecorder::eventFilter(QObject *receiver, QEvent *event) {
	switch (event->type()) {
	case QEvent::MouseButtonPress:
	case QEvent::MouseButtonRelease:
	case QEvent::MouseButtonDblClick:
	case QEvent::MouseMove:
		break;
	default:
		return false;
	}
	if (receiver == _window.data()) {
		++_windowEvents;
		if (!_delivered && (event->type() == _type)) {
			_delivered = true;
			_spontaneous = event->spontaneous();
		}
	} else if (_delivered
		&& !_receiverSeen
		&& (event->type() == _type)
		&& receiver->isWidgetType()) {
		_receiverSeen = true;
		_receiver = WidgetDescription(static_cast<QWidget*>(receiver));
		_receiverSpontaneous = event->spontaneous();
	}
	return false;
}
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API

[[nodiscard]] QString FlagText(bool value) {
	return value ? u"1"_q : u"0"_q;
}

[[nodiscard]] QString PointText(QPoint point) {
	return u"%1,%2"_q.arg(point.x()).arg(point.y());
}

[[nodiscard]] QString NumberText(double value) {
	return QString::number(value, 'g', QLocale::FloatingPointShortest);
}

[[nodiscard]] QString PointText(QPointF point) {
	return u"%1,%2"_q.arg(NumberText(point.x()), NumberText(point.y()));
}

[[nodiscard]] QString DescriptionText(const QString &description) {
	return description.isEmpty() ? u"none"_q : description;
}

[[nodiscard]] QString Describe(QWidget *widget) {
	return widget ? WidgetDescription(widget) : QString();
}

[[nodiscard]] QString TypeName(QpaMouseType type) {
	switch (type) {
	case QpaMouseType::Press: return u"press"_q;
	case QpaMouseType::Move: return u"move"_q;
	case QpaMouseType::Release: return u"release"_q;
	}
	Unexpected("Type in Test::QpaMouseDeliveryText.");
}

// The top-level itself and its widgets in the same window that Qt marks
// with Qt::WA_UnderMouse.
[[nodiscard]] QStringList HoveredWidgets(QWidget *top) {
	auto result = QStringList();
	if (!top) {
		return result;
	}
	if (top->underMouse()) {
		result.push_back(WidgetDescription(top));
	}
	for (const auto child : top->findChildren<QWidget*>()) {
		if ((child->window() == top) && child->underMouse()) {
			result.push_back(WidgetDescription(child));
		}
	}
	return result;
}

#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
[[nodiscard]] QEvent::Type EventType(QpaMouseType type) {
	switch (type) {
	case QpaMouseType::Press: return QEvent::MouseButtonPress;
	case QpaMouseType::Move: return QEvent::MouseMove;
	case QpaMouseType::Release: return QEvent::MouseButtonRelease;
	}
	Unexpected("Type in Test::QpaMouseSequence::deliver.");
}

// QWidgetWindow::handleMouseEvent takes its popup branch for any
// QApplication::activePopupWidget(), and QGuiApplicationPrivate forwards
// to a QWindow popup as well.
[[nodiscard]] QString ActivePopupText() {
	if (const auto popup = QApplication::activePopupWidget()) {
		return WidgetDescription(popup);
	}
	const auto window = QGuiApplicationPrivate::activePopupWindow();
	return window
		? QString::fromLatin1(window->metaObject()->className())
		: QString();
}

[[nodiscard]] bool BlockedByModal(not_null<QWindow*> window) {
	return QWindowPrivate::get(window)->blockedByModalWindow;
}

[[nodiscard]] ResolvedWindow Refused(
		QpaMouseRefusal refusal,
		const QString &reason) {
	return { .reason = reason, .refusal = refusal };
}

[[nodiscard]] ResolvedWindow ResolveWindow(QWidget *target, bool hadTarget) {
	if (!target) {
		return hadTarget
			? Refused(
				QpaMouseRefusal::TargetDestroyed,
				u"the target was destroyed; nothing was sent"_q)
			: Refused(
				QpaMouseRefusal::NoTarget,
				u"no target widget was given; nothing was sent"_q);
	}
	const auto top = target->window();
	const auto window = top->windowHandle();
	if (!window) {
		return Refused(
			QpaMouseRefusal::NoWindowHandle,
			u"%1 has no QWindow, as a top-level never shown, and none is "
			u"created here; nothing was sent"_q.arg(WidgetDescription(top)));
	}
	const auto dontShow = top->testAttribute(Qt::WA_DontShowOnScreen);
	if (dontShow || !top->isVisible() || !window->isVisible()) {
		return Refused(
			QpaMouseRefusal::WindowHidden,
			u"the target's window is not on screen: widgetVisible=%1 "
			u"windowVisible=%2 dontShowOnScreen=%3; nothing was sent"_q.arg(
				FlagText(top->isVisible()),
				FlagText(window->isVisible()),
				FlagText(dontShow)));
	}
	const auto popup = ActivePopupText();
	if (!popup.isEmpty()) {
		return Refused(
			QpaMouseRefusal::ActivePopup,
			u"a popup is open (%1), and Qt would route the event through "
			u"its popup path; nothing was sent"_q.arg(popup));
	} else if (BlockedByModal(window)) {
		return Refused(
			QpaMouseRefusal::BlockedByModal,
			u"a modal window blocks the target's window, so Qt would drop "
			u"the event; nothing was sent"_q);
	}
	return { .top = top, .window = window };
}

// A release of each of |held| through |window| at the sequence's last
// native points, each inside Settle. Qt's last cursor position is put on
// that point first, so Qt synthesizes no held move before the release.
void ReleaseThrough(
		not_null<QWindow*> window,
		Qt::MouseButtons held,
		QPointF nativeLocal,
		QPointF nativeGlobal,
		Qt::KeyboardModifiers modifiers) {
	const auto guard = QPointer<QWindow>(window.get());
	auto remaining = held;
	while (remaining && guard) {
		const auto bits = uint(remaining.toInt());
		const auto button = Qt::MouseButton(1u << std::countr_zero(bits));
		remaining.setFlag(button, false);
		Settle([&] {
			QGuiApplicationPrivate::lastCursorPosition
				= QHighDpi::fromNativeGlobalPosition(
					nativeGlobal,
					window.get());
			QWindowSystemInterface::handleMouseEvent<Delivery>(
				window,
				nativeLocal,
				nativeGlobal,
				remaining,
				button,
				QEvent::MouseButtonRelease,
				modifiers);
		});
	}
}
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API

void Refuse(
		QpaMouseDrag &reading,
		QpaMouseRefusal refusal,
		const QString &reason) {
	if (reading.refusal == QpaMouseRefusal::None) {
		reading.refusal = refusal;
		reading.reason = reason;
	}
}

// After one gesture delivery: true while the gesture goes on. The first
// refusal stops it and stays. Once |then| or the finish callback took the
// reading - inside a nested event loop the delivery entered - nothing is
// written.
[[nodiscard]] bool ContinueAfter(
		QpaMouseDragDriver &driver,
		const QpaMouseDelivery &delivery,
		const QString &what) {
	if (driver.closed) {
		return false;
	}
	auto &reading = *driver.reading;
	if (delivery.refusal != QpaMouseRefusal::None) {
		Refuse(
			reading,
			delivery.refusal,
			u"the %1 was refused: %2"_q.arg(what, delivery.reason));
	} else if (!delivery.targetAlive) {
		Refuse(
			reading,
			QpaMouseRefusal::TargetDestroyed,
			u"the target was destroyed while its %1 was handled; nothing "
			u"more was sent to it"_q.arg(what));
	} else {
		return true;
	}
	driver.stopped = true;
	return false;
}

// The next interpolated held move, at Test::Drag's point for that step.
[[nodiscard]] bool DeliverNextMove(QpaMouseDragDriver &driver) {
	auto &reading = *driver.reading;
	const auto step = ++driver.step;
	const auto delta = reading.to - reading.from;
	const auto move = driver.sequence->deliver({
		.type = QpaMouseType::Move,
		.point = reading.from + (delta * step) / reading.steps,
		.buttons = Qt::LeftButton,
		.modifiers = driver.args.modifiers,
	});
	if (!driver.closed && move.delivered) {
		++reading.moves;
	}
	return ContinueAfter(driver, move, u"move %1"_q.arg(step));
}

// Posted after the last move's settle, so they run behind every call the
// press and the moves queued, each in its own FIFO queue.
void PostMarkers(QpaMouseDragDriver &driver) {
	if (const auto target = driver.target.data()) {
		driver.markers = PostQueuedWorkMarkers(target);
	}
}

void PaceNextMove(QpaMouseDragDriver &driver) {
	if (driver.closed
		|| driver.stopped
		|| !driver.target
		|| !DeliverNextMove(driver)) {
		driver.pace.cancel();
	} else if (driver.step >= driver.reading->steps) {
		driver.pace.cancel();
		PostMarkers(driver);
	}
}

void StartQpaMouseDrag(QpaMouseDragDriver &driver) {
	const auto &args = driver.args;
	auto &reading = *driver.reading;
	const auto resolved = args.resolve
		? args.resolve()
		: QpaMouseDragTarget();
	reading.from = resolved.from;
	reading.to = resolved.to;
	reading.target = Describe(resolved.widget);
	driver.target = resolved.widget;
	driver.sequence = std::make_unique<QpaMouseSequence>(resolved.widget);
	const auto press = driver.sequence->deliver({
		.type = QpaMouseType::Press,
		.point = reading.from,
		.button = Qt::LeftButton,
		.buttons = Qt::LeftButton,
		.modifiers = args.modifiers,
	});
	if (!driver.closed) {
		reading.pressed = press.delivered;
		reading.pressReceiver = press.receiver;
		reading.grabber = press.grabber;
	}
	if (!ContinueAfter(driver, press, u"press"_q)) {
		return;
	} else if (args.paceMs > 0) {
		driver.pace.callEach(args.paceMs);
		return;
	}
	while (driver.step < reading.steps) {
		if (!DeliverNextMove(driver)) {
			return;
		}
	}
	PostMarkers(driver);
}

[[nodiscard]] bool QpaMouseDragSettled(const QpaMouseDragDriver &driver) {
	const auto markers = driver.markers.get();
	return driver.stopped
		|| !driver.target
		|| (markers
			&& (markers->ran()
				|| (crl::now() - markers->postedAt >= driver.args.bound)));
}

[[nodiscard]] QString QueuesText(const QpaMouseDrag &reading) {
	return u"qtQueue=%1 mainQueue=%2"_q.arg(
		FlagText(reading.qtQueueRan),
		FlagText(reading.mainQueueRan));
}

void ReadMarkers(QpaMouseDragDriver &driver) {
	if (const auto markers = driver.markers.get()) {
		auto &reading = *driver.reading;
		reading.qtQueueRan = markers->qtQueueRan;
		reading.mainQueueRan = markers->mainQueueRan;
		reading.waitedMs = crl::now() - markers->postedAt;
	}
}

void FinishQpaMouseDrag(QpaMouseDragDriver &driver) {
	driver.pace.cancel();
	if (driver.closed) {
		return;
	}
	driver.closed = true;
	ReadMarkers(driver);
	auto &reading = *driver.reading;
	const auto sequence = driver.sequence.get();
	const auto holding = sequence && (sequence->held() & Qt::LeftButton);
	if (holding && driver.target) {
		if (driver.markers && !(reading.qtQueueRan && reading.mainQueueRan)) {
			Refuse(
				reading,
				QpaMouseRefusal::BoundExpired,
				u"the markers had not both run %1 ms after they were posted "
				u"(%2); the release was sent anyway, so the order is not "
				u"established"_q.arg(
					QString::number(driver.args.bound),
					QueuesText(reading)));
		}
		const auto release = sequence->deliver({
			.type = QpaMouseType::Release,
			.point = reading.to,
			.button = Qt::LeftButton,
			.buttons = Qt::NoButton,
			.modifiers = driver.args.modifiers,
		});
		reading.released = (release.refusal == QpaMouseRefusal::None)
			&& release.delivered;
		reading.releaseReceiver = release.receiver;
		if (release.refusal != QpaMouseRefusal::None) {
			Refuse(
				reading,
				release.refusal,
				u"the release was refused: %1"_q.arg(release.reason));
		}
	} else if (holding) {
		Refuse(
			reading,
			QpaMouseRefusal::TargetDestroyed,
			u"the target was destroyed while the gesture went on (%1); no "
			u"release was sent to it"_q.arg(QueuesText(reading)));
	}
	if (sequence) {
		reading.end = sequence->end();
	}
	reading.finished = true;
	Note(QpaMouseDragText(reading));
}

// Runs on every finish path. After |then| it only repeats end(), which
// then sends nothing; a stage the runner left unfinished is ended here
// and its reading marked aborted.
void AbortQpaMouseDrag(QpaMouseDragDriver &driver) {
	driver.pace.cancel();
	const auto sequence = driver.sequence.get();
	if (!sequence) {
		return;
	}
	const auto end = sequence->end();
	if (driver.closed) {
		return;
	}
	driver.closed = true;
	ReadMarkers(driver);
	auto &reading = *driver.reading;
	reading.end = end;
	reading.aborted = true;
	if (reading.reason.isEmpty()) {
		reading.reason = u"the runner finished before the stage's then; "
			u"Runner::onFinish ended the sequence"_q;
	}
	Note(QpaMouseDragText(reading));
}

[[nodiscard]] crl::time QpaMouseDragTimeout(const QpaMouseDrag &reading) {
	return std::max(reading.bound, crl::time())
		+ std::max(reading.paceMs, crl::time()) * reading.steps
		+ kDefaultStageTimeout;
}

} // namespace

QString QpaMouseRefusalName(QpaMouseRefusal refusal) {
	switch (refusal) {
	case QpaMouseRefusal::None: return u"none"_q;
	case QpaMouseRefusal::Unsupported: return u"unsupported"_q;
	case QpaMouseRefusal::NoTarget: return u"no-target"_q;
	case QpaMouseRefusal::TargetDestroyed: return u"target-destroyed"_q;
	case QpaMouseRefusal::NoWindowHandle: return u"no-window-handle"_q;
	case QpaMouseRefusal::WindowHidden: return u"window-hidden"_q;
	case QpaMouseRefusal::ActivePopup: return u"active-popup"_q;
	case QpaMouseRefusal::BlockedByModal: return u"blocked-by-modal"_q;
	case QpaMouseRefusal::BoundExpired: return u"bound-expired"_q;
	}
	Unexpected("Refusal in Test::QpaMouseRefusalName.");
}

QString QpaMouseDeliveryText(const QpaMouseDelivery &reading) {
	const auto line = u"%1 sent=%2 delivered=%3 accepted=%4 windowEvents=%5 "
		u"spontaneous=%6 receiver=%7 receiverSpontaneous=%8 grabber=%9 "
		u"buttons=%10 targetAlive=%11 local=%12 global=%13 nativeLocal=%14 "
		u"nativeGlobal=%15 refusal=%16"_q.arg(
			TypeName(reading.type),
			FlagText(reading.sent),
			FlagText(reading.delivered),
			FlagText(reading.accepted),
			QString::number(reading.windowEvents),
			FlagText(reading.spontaneous),
			DescriptionText(reading.receiver),
			FlagText(reading.receiverSpontaneous),
			DescriptionText(reading.grabber),
			QString::number(reading.buttons.toInt()),
			FlagText(reading.targetAlive),
			PointText(reading.local),
			PointText(reading.global),
			PointText(reading.nativeLocal),
			PointText(reading.nativeGlobal),
			QpaMouseRefusalName(reading.refusal));
	return reading.reason.isEmpty()
		? line
		: (line + u" - "_q + reading.reason);
}

QString QpaMouseEndText(const QpaMouseEnd &reading) {
	return u"ended=%1 held=%2 releaseSent=%3 targetAlive=%4 "
		u"buttonsCleared=%5 grabReleased=%6 leaveSent=%7 buttons=%8 "
		u"grabber=%9 hovered=[%10] clean=%11"_q.arg(
			FlagText(reading.ended),
			QString::number(reading.held.toInt()),
			FlagText(reading.releaseSent),
			FlagText(reading.targetAlive),
			FlagText(reading.buttonsCleared),
			FlagText(reading.grabReleased),
			FlagText(reading.leaveSent),
			QString::number(reading.buttons.toInt()),
			DescriptionText(reading.grabber),
			reading.hovered.join(u"; "_q),
			FlagText(reading.clean()));
}

QString QpaMouseDragText(const QpaMouseDrag &reading) {
	const auto line = u"QPA_MOUSE_DRAG: name=%1 finished=%2 aborted=%3 "
		u"target=%4 from=%5 to=%6 steps=%7 paceMs=%8 bound=%9 waitedMs=%10 "
		u"pressed=%11 moves=%12 qtQueue=%13 mainQueue=%14 released=%15 "
		u"pressReceiver=%16 grabber=%17 releaseReceiver=%18 refusal=%19 "
		u"end=[%20]"_q.arg(
			reading.name,
			FlagText(reading.finished),
			FlagText(reading.aborted),
			DescriptionText(reading.target),
			PointText(reading.from),
			PointText(reading.to),
			QString::number(reading.steps),
			QString::number(reading.paceMs),
			QString::number(reading.bound),
			QString::number(reading.waitedMs),
			FlagText(reading.pressed),
			QString::number(reading.moves),
			FlagText(reading.qtQueueRan),
			FlagText(reading.mainQueueRan),
			FlagText(reading.released),
			DescriptionText(reading.pressReceiver),
			DescriptionText(reading.grabber),
			DescriptionText(reading.releaseReceiver),
			QpaMouseRefusalName(reading.refusal),
			QpaMouseEndText(reading.end));
	return reading.reason.isEmpty()
		? line
		: (line + u" - "_q + reading.reason);
}

QpaMouseSequence::QpaMouseSequence(QWidget *target)
: _target(target)
, _hadTarget(target != nullptr) {
}

QpaMouseSequence::~QpaMouseSequence() = default;

QpaMouseDelivery QpaMouseSequence::deliver(const QpaMouseEvent &event) {
	auto result = QpaMouseDelivery();
	result.type = event.type;
#ifdef DESKTOP_APP_DISABLE_PRIVATE_QT_API
	result.refusal = QpaMouseRefusal::Unsupported;
	result.reason = u"built with DESKTOP_APP_DISABLE_PRIVATE_QT_API: there "
		u"is no QWindowSystemInterface delivery; nothing was sent"_q;
	return result;
#else // DESKTOP_APP_DISABLE_PRIVATE_QT_API
	const auto target = _target.data();
	const auto resolved = ResolveWindow(target, _hadTarget);
	if (resolved.refusal != QpaMouseRefusal::None) {
		result.refusal = resolved.refusal;
		result.reason = resolved.reason;
		return result;
	}
	const auto window = resolved.window;
	const auto point = QPointF(event.point);
	result.local = target->mapTo(resolved.top, point);
	result.global = target->mapToGlobal(point);
	result.nativeLocal = QHighDpi::toNativeLocalPosition(
		result.local,
		window);
	result.nativeGlobal = QHighDpi::toNativeGlobalPosition(
		result.global,
		window);

	// Kept before the dispatch, so end() needs neither this target alive
	// nor another mapping.
	if (!_sent) {
		_grabberBefore = QWidget::mouseGrabber();
	}
	_sent = true;
	_open = true;
	_top = resolved.top;
	_window = window;
	_lastNativeLocal = result.nativeLocal;
	_lastNativeGlobal = result.nativeGlobal;
	_modifiers = event.modifiers;
	if (event.type != QpaMouseType::Move) {
		_held.setFlag(event.button, event.type == QpaMouseType::Press);
	}

	const auto type = EventType(event.type);
	result.sent = true;
	Settle([&] {
		if (event.type == QpaMouseType::Press) {
			QGuiApplicationPrivate::mousePressButton = Qt::NoButton;
			QGuiApplicationPrivate::lastCursorPosition
				= QHighDpi::fromNativeGlobalPosition(
					result.nativeGlobal,
					window);
		}
		auto recorder = DeliveryRecorder(window, type);
		result.accepted = QWindowSystemInterface::handleMouseEvent<Delivery>(
			window,
			result.nativeLocal,
			result.nativeGlobal,
			event.buttons,
			event.button,
			type,
			event.modifiers);
		recorder.read(result);
	});
	result.targetAlive = !_target.isNull();
	result.buttons = QGuiApplication::mouseButtons();
	result.grabber = Describe(QWidget::mouseGrabber());
	return result;
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API
}

QpaMouseEnd QpaMouseSequence::end() {
	if (_end.ended && !_open) {
		return _end;
	}
	auto result = QpaMouseEnd();
	result.ended = true;
	result.held = base::take(_held);
	result.targetAlive = !_target.isNull();
#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
	if (_open) {
		const auto window = _window.data();
		if (result.held
			&& window
			&& ActivePopupText().isEmpty()
			&& !BlockedByModal(window)) {
			ReleaseThrough(
				window,
				result.held,
				_lastNativeLocal,
				_lastNativeGlobal,
				_modifiers);
			result.releaseSent = true;
		}
		const auto left = QGuiApplicationPrivate::mouse_buttons
			& result.held;
		if (left) {
			QGuiApplicationPrivate::mouse_buttons &= ~left;
			result.buttonsCleared = true;
		}
		const auto grabber = QWidget::mouseGrabber();
		if (grabber && (grabber != _grabberBefore.data())) {
			grabber->releaseMouse();
			result.grabReleased = true;
		}
		if (const auto alive = _window.data()) {
			Settle([&] {
				QWindowSystemInterface::handleLeaveEvent<Delivery>(alive);
			});
			result.leaveSent = true;
		}
	}
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API
	result.buttons = QGuiApplication::mouseButtons();
	result.grabber = Describe(QWidget::mouseGrabber());
	result.hovered = HoveredWidgets(_top.data());
	_open = false;
	_end = result;
	return result;
}

Qt::MouseButtons QpaMouseSequence::held() const {
	return _held;
}

std::shared_ptr<const QpaMouseDrag> AppendQpaMouseDrag(
		not_null<Runner*> runner,
		QpaMouseDragArgs args) {
	const auto driver = std::make_shared<QpaMouseDragDriver>();
	driver->args = std::move(args);
	driver->pace.setCallback([raw = driver.get()] {
		PaceNextMove(*raw);
	});
	driver->reading = std::make_shared<QpaMouseDrag>();
	auto &reading = *driver->reading;
	reading.name = driver->args.name;
	reading.bound = driver->args.bound;
	reading.paceMs = driver->args.paceMs;
	reading.steps = std::max(driver->args.steps, 1);
	runner->onFinish([=] {
		AbortQpaMouseDrag(*driver);
	});
	runner->add({
		.name = u"QPA mouse drag: %1"_q.arg(driver->args.name),
		.skipReason = driver->args.skipReason,
		.run = [=] {
			StartQpaMouseDrag(*driver);
		},
		.until = [=] {
			return QpaMouseDragSettled(*driver);
		},
		.then = [=] {
			FinishQpaMouseDrag(*driver);
		},
		.timeout = QpaMouseDragTimeout(reading),
		.timeoutDetails = [=] {
			return QpaMouseDragText(*driver->reading);
		},
	});
	return driver->reading;
}

} // namespace Test

#endif // _DEBUG
