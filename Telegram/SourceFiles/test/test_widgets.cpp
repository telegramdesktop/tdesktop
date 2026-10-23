/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_widgets.h"

#include "base/flat_map.h"
#include "base/weak_qptr.h"
#include "core/sandbox.h"
#include "test/test_agent.h"
#include "test/test_capture.h"
#include "test/test_log.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/abstract_button.h"

#include <QtCore/QMimeData>
#include <QtCore/QPointer>
#include <QtCore/QTextBoundaryFinder>
#include <QtGui/QCursor>
#include <QtGui/QDragEnterEvent>
#include <QtGui/QDragLeaveEvent>
#include <QtGui/QDropEvent>
#include <QtGui/QGuiApplication>
#include <QtGui/QInputMethodEvent>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>
#include <QtWidgets/QTextEdit>

#include <cmath>

#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
#include <private/qdnd_p.h>
#include <qpa/qwindowsysteminterface.h>
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API

namespace Test {
namespace {

#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
// Withholds every drag event from any receiver other than |target| for its
// own lifetime. Returning true from an application filter skips delivery,
// and QApplication::notify still climbs an un-accepted DragEnter past it,
// so the filter sees, and records, every widget Qt would have offered it to.
class DropShield final : public QObject {
public:
	explicit DropShield(not_null<QWidget*> target);
	~DropShield();

	[[nodiscard]] int targetEnters() const;
	[[nodiscard]] QStringList shielded() const;

protected:
	bool eventFilter(QObject *receiver, QEvent *event) override;

private:
	QWidget *_target = nullptr;
	int _targetEnters = 0;
	QStringList _shielded;

};
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API

// ClickWithPressOrigin's press-scoped filter. Installed on the target
// itself with installEventFilter - never parented to it, so the target
// gets no ChildAdded - and Qt prepends object filters, so it runs before
// every filter installed earlier. It withholds the target's QEvent::Move
// only during its own moveTo() calls, so a move the press handler makes
// passes through, and counts every QEvent::Paint reaching the target
// while setDisplaced(true) is in force.
class PressOriginGuard final : public QObject {
public:
	explicit PressOriginGuard(not_null<QWidget*> target);
	~PressOriginGuard();

	void moveTo(QPoint position);
	void setDisplaced(bool displaced);
	[[nodiscard]] int withheld() const;
	[[nodiscard]] int paints() const;

protected:
	bool eventFilter(QObject *receiver, QEvent *event) override;

private:
	QPointer<QWidget> _target;
	int _withheld = 0;
	int _paints = 0;
	bool _withholding = false;
	bool _displaced = false;

};

struct LiveWidgetEntry {
	QPointer<QWidget> widget;
	int generation = 0;
};

struct LiveActionEntry {
	QPointer<QObject> context;
	std::shared_ptr<Fn<void()>> action;
	int generation = 0;
	int invocationCount = 0;
	bool repeatable = false;
};

[[nodiscard]] base::flat_map<QString, LiveWidgetEntry> &LiveWidgets() {
	static auto result = base::flat_map<QString, LiveWidgetEntry>();
	return result;
}

[[nodiscard]] base::flat_map<QString, LiveActionEntry> &LiveActions() {
	static auto result = base::flat_map<QString, LiveActionEntry>();
	return result;
}

bool DeliverAndSettle(
		const base::weak_qptr<QWidget> &widget,
		QEvent &event) {
	const auto strong = widget.get();
	if (!strong) {
		return false;
	}
	Settle([&] {
		QApplication::sendEvent(strong, &event);
	});
	return (widget.get() != nullptr);
}

void DeliverPointerLeave(const base::weak_qptr<QWidget> &widget) {
	auto leave = QEvent(QEvent::Leave);
	DeliverAndSettle(widget, leave);
}

// The one event Drag's press, moves and release are built from. Each event
// is still built right before its own delivery, so mapToGlobal is read only
// after the previous event settled.
[[nodiscard]] QMouseEvent MakeDragEvent(
		not_null<QWidget*> widget,
		QEvent::Type type,
		QPoint local,
		Qt::MouseButton button,
		Qt::KeyboardModifiers modifiers) {
	return QMouseEvent(
		type,
		QPointF(local),
		QPointF(widget->mapToGlobal(local)),
		button,
		(type == QEvent::MouseButtonRelease)
			? Qt::NoButton
			: Qt::LeftButton,
		modifiers);
}

[[nodiscard]] int &ActivationAttempts() {
	static auto result = 0;
	return result;
}

[[nodiscard]] int &ActivationNotes() {
	static auto result = 0;
	return result;
}

// A non-null |widget| whose own window carries no QWindow handle is refused
// rather than silently retargeted at some other top-level: activating a
// window the caller never named would make every field of the returned
// reading a claim about a widget it was not taken on. Only the no-widget
// form may search, because it names no target to be wrong about.
[[nodiscard]] QWindow *ResolveActivationWindow(QWidget *widget) {
	if (widget) {
		const auto top = widget->window();
		return top ? top->windowHandle() : nullptr;
	} else if (const auto active = QGuiApplication::focusWindow()) {
		return active;
	}
	for (const auto window : QGuiApplication::topLevelWindows()) {
		if (window->isVisible()) {
			return window;
		}
	}
	return nullptr;
}

void InjectActivation(QWindow *window) {
#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
#if QT_VERSION >= QT_VERSION_CHECK(6, 3, 0)
	QWindowSystemInterface::handleFocusWindowChanged(
		window,
		Qt::ActiveWindowFocusReason);
#else // Qt >= 6.3.0
	QWindowSystemInterface::handleWindowActivated(
		window,
		Qt::ActiveWindowFocusReason);
#endif // Qt < 6.3.0
	QWindowSystemInterface::flushWindowSystemEvents();
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API
}

[[nodiscard]] QString WheelStopRefusal(
		const QString &start,
		const QString &stop,
		const QString &inert,
		bool isWindow,
		bool noMousePropagation) {
	const auto stopText = u"started at %1; stopped at %2 (window=%3 "
		u"noMousePropagation=%4)"_q.arg(start, stop).arg(
			isWindow ? 1 : 0).arg(noMousePropagation ? 1 : 0);
	return inert.isEmpty()
		? (u"no widget consumed the wheel: "_q + stopText)
		: (u"%1 returned the event still accepted without handling it; "_q.arg(
			inert) + stopText);
}

[[nodiscard]] QString ViewportRefusal(not_null<QWidget*> viewport) {
	return u"the editor viewport does not take drops (enabled=%1 "
		u"acceptDrops=%2), so Qt would offer the enter to the next enabled "
		u"ancestor that accepts drops; nothing was sent"_q
		.arg(viewport->isEnabled() ? 1 : 0)
		.arg(viewport->acceptDrops() ? 1 : 0);
}

#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
[[nodiscard]] QString ShieldedRefusal(const QStringList &shielded) {
	return u"the field's editor did not accept the enter; the enter was "
		u"withheld from %1 other widget(s) Qt would have offered it to: "
		u"[%2]; no drop was sent"_q
		.arg(shielded.size())
		.arg(shielded.join(u"; "_q));
}

DropShield::DropShield(not_null<QWidget*> target)
: _target(target) {
	QCoreApplication::instance()->installEventFilter(this);
}

DropShield::~DropShield() {
	if (const auto instance = QCoreApplication::instance()) {
		instance->removeEventFilter(this);
	}
}

int DropShield::targetEnters() const {
	return _targetEnters;
}

QStringList DropShield::shielded() const {
	return _shielded;
}

bool DropShield::eventFilter(QObject *receiver, QEvent *event) {
	const auto type = event->type();
	if (type != QEvent::DragEnter
		&& type != QEvent::DragMove
		&& type != QEvent::DragLeave
		&& type != QEvent::Drop) {
		return false;
	} else if (receiver == _target) {
		if (type == QEvent::DragEnter) {
			++_targetEnters;
		}
		return false;
	}
	const auto widget = qobject_cast<QWidget*>(receiver);
	const auto description = widget
		? WidgetDescription(widget)
		: QString::fromLatin1(receiver->metaObject()->className());
	if (!_shielded.contains(description)) {
		_shielded.push_back(description);
	}
	return true;
}
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API

PressOriginGuard::PressOriginGuard(not_null<QWidget*> target)
: _target(target.get()) {
	target->installEventFilter(this);
}

PressOriginGuard::~PressOriginGuard() {
	if (const auto target = _target.data()) {
		target->removeEventFilter(this);
	}
}

void PressOriginGuard::moveTo(QPoint position) {
	if (const auto target = _target.data()) {
		_withholding = true;
		target->move(position);
		_withholding = false;
	}
}

void PressOriginGuard::setDisplaced(bool displaced) {
	_displaced = displaced;
}

int PressOriginGuard::withheld() const {
	return _withheld;
}

int PressOriginGuard::paints() const {
	return _paints;
}

bool PressOriginGuard::eventFilter(QObject *receiver, QEvent *event) {
	if (receiver != _target.data()) {
		return false;
	} else if (_withholding && event->type() == QEvent::Move) {
		++_withheld;
		return true;
	} else if (_displaced && event->type() == QEvent::Paint) {
		++_paints;
	}
	return false;
}

[[nodiscard]] QString ActivationWindowIdentity(QWindow *window) {
	if (!window) {
		return u"no window"_q;
	}
	const auto name = window->objectName();
	const auto geometry = window->geometry();
	return u"%1 %2,%3 %4x%5"_q
		.arg(name.isEmpty() ? u"application focus window"_q : name)
		.arg(geometry.x())
		.arg(geometry.y())
		.arg(geometry.width())
		.arg(geometry.height());
}

[[nodiscard]] QString PointText(QPoint point) {
	return u"%1,%2"_q.arg(point.x()).arg(point.y());
}

// The target itself, or the first descendant inside its window, that is a
// native widget. A separate window parented below the target is skipped
// with its own children: moving the target moves none of them.
[[nodiscard]] QWidget *FindNativeWindow(not_null<QWidget*> widget) {
	if (widget->internalWinId()
		|| widget->testAttribute(Qt::WA_NativeWindow)) {
		return widget;
	}
	const auto children = widget->findChildren<QWidget*>(
		QString(),
		Qt::FindDirectChildrenOnly);
	for (const auto child : children) {
		if (child->isWindow()) {
			continue;
		} else if (const auto native = FindNativeWindow(child)) {
			return native;
		}
	}
	return nullptr;
}

[[nodiscard]] QString NativeTargetReason(
		not_null<QWidget*> target,
		not_null<QWidget*> native) {
	return u"native widget %1 (%2) has internalWinId=%3 "
		u"WA_NativeWindow=%4: moving the target would move a real platform "
		u"window, and a native widget maps through its own QWindow; nothing "
		u"was moved or sent"_q
		.arg(WidgetDescription(native))
		.arg((native == target)
			? u"the target itself"_q
			: u"a descendant of the target"_q)
		.arg(native->internalWinId() ? 1 : 0)
		.arg(native->testAttribute(Qt::WA_NativeWindow) ? 1 : 0);
}

[[nodiscard]] QString OriginUnreachableReason(
		const PressOrigin &reading,
		QPointF exact,
		QPoint cursorSeen) {
	return u"after moving the target by %1 the cursor maps to %2, not to "
		u"the press point %3 (floating mapping before the move %4,%5; "
		u"cursor %6 then %7): a transformed ancestor, a cursor that moved "
		u"between the reads, or a point coordinate of 0 met on an exact "
		u"half pixel; the target was moved back and nothing was sent"_q
		.arg(PointText(reading.displacement))
		.arg(PointText(reading.originSeen))
		.arg(PointText(reading.point))
		.arg(exact.x(), 0, 'f', 2)
		.arg(exact.y(), 0, 'f', 2)
		.arg(PointText(reading.cursorBefore))
		.arg(PointText(cursorSeen));
}

// The body ClickWithPressOrigin runs inside its Test::Settle action:
// displace, verify, send, restore - all before Settle's drain. |target| is
// never touched after the press except through |alive|, because the press
// may destroy it; the guard holds its own QPointer.
void DeliverPressWithOrigin(
		not_null<QWidget*> target,
		const base::weak_qptr<QWidget> &alive,
		QMouseEvent &press,
		PressOrigin &result) {
	result.cursorBefore = QCursor::pos();
	const auto exact = target->mapFromGlobal(QPointF(result.cursorBefore));
	result.displacement = QPoint(
		int(std::floor(exact.x() - result.point.x() + 0.5)),
		int(std::floor(exact.y() - result.point.y() + 0.5)));
	const auto original = target->pos();
	const auto originalSize = target->size();
	const auto displacedAt = original + result.displacement;
	const auto moved = target->testAttribute(Qt::WA_Moved);
	const auto pendingMove = target->testAttribute(Qt::WA_PendingMoveEvent);
	const auto noPropagation = target->testAttribute(
		Qt::WA_NoMousePropagation);
	const auto restoreMoveAttributes = [&](not_null<QWidget*> widget) {
		widget->setAttribute(Qt::WA_Moved, moved);
		widget->setAttribute(Qt::WA_PendingMoveEvent, pendingMove);
	};

	auto guard = PressOriginGuard(target);
	const auto readGuard = gsl::finally([&] {
		result.moveEventsWithheld = guard.withheld();
		result.paintsWhileDisplaced = guard.paints();
	});
	target->setAttribute(Qt::WA_NoMousePropagation);
	if (!result.displacement.isNull()) {
		guard.moveTo(displacedAt);
		result.displaced = true;
	}
	guard.setDisplaced(true);
	const auto cursorSeen = QCursor::pos();
	result.originSeen = target->mapFromGlobal(cursorSeen);
	if (result.originSeen != result.point) {
		guard.setDisplaced(false);
		if (result.displaced) {
			guard.moveTo(original);
		}
		restoreMoveAttributes(target);
		target->setAttribute(Qt::WA_NoMousePropagation, noPropagation);
		result.restored = (target->pos() == original);
		result.refusal = PressOriginRefusal::OriginUnreachable;
		result.reason = OriginUnreachableReason(result, exact, cursorSeen);
		return;
	}

	QApplication::sendEvent(target.get(), &press);
	result.pressSent = true;
	result.pressAccepted = press.isAccepted();
	result.cursorAfter = QCursor::pos();
	result.cursorStable = (result.cursorAfter == result.cursorBefore);
	guard.setDisplaced(false);
	const auto strong = alive.get();
	if (!strong) {
		return;
	}
	const auto expected = result.displaced ? displacedAt : original;
	if (strong->pos() != expected) {
		result.geometryChangedByPress = true;
	} else {
		if (result.displaced) {
			guard.moveTo(original);
		}
		restoreMoveAttributes(strong);
	}
	if (strong->size() != originalSize) {
		result.geometryChangedByPress = true;
	}
	strong->setAttribute(Qt::WA_NoMousePropagation, noPropagation);
	result.restored = (strong->pos() == original);
}

} // namespace

QWidget *FindByObjectName(
		not_null<QWidget*> root,
		const QString &name) {
	return root->findChild<QWidget*>(name);
}

void PublishLiveWidget(
		const QString &key,
		not_null<QWidget*> widget) {
	if (!Active()) {
		return;
	}
	auto &entry = LiveWidgets()[key];
	entry = {
		.widget = widget.get(),
		.generation = entry.generation + 1,
	};
}

LiveWidgetSnapshot ReadLiveWidget(const QString &key) {
	const auto i = LiveWidgets().find(key);
	if (i == end(LiveWidgets())) {
		return {};
	}
	return {
		.widget = i->second.widget.data(),
		.generation = i->second.generation,
	};
}

void PublishLiveAction(
		const QString &key,
		not_null<QObject*> context,
		Fn<void()> action,
		bool repeatable) {
	if (!Active()) {
		return;
	}
	const auto callback = action
		? std::make_shared<Fn<void()>>(std::move(action))
		: std::shared_ptr<Fn<void()>>();
	auto &entry = LiveActions()[key];
	entry = {
		.context = context.get(),
		.action = callback,
		.generation = entry.generation + 1,
		.invocationCount = 0,
		.repeatable = repeatable,
	};
}

LiveActionSnapshot ReadLiveAction(const QString &key) {
	const auto i = LiveActions().find(key);
	if (i == end(LiveActions())) {
		return {};
	}
	const auto &entry = i->second;
	return {
		.available = entry.context
			&& entry.action
			&& (entry.repeatable || !entry.invocationCount),
		.generation = entry.generation,
		.invocationCount = entry.invocationCount,
		.repeatable = entry.repeatable,
	};
}

bool InvokeLiveAction(const QString &key) {
	auto i = LiveActions().find(key);
	if (i == end(LiveActions())) {
		return false;
	}
	auto &entry = i->second;
	if (!entry.context
		|| !entry.action
		|| (!entry.repeatable && entry.invocationCount)) {
		return false;
	}
	++entry.invocationCount;
	const auto action = entry.action;
	(*action)();
	return true;
}

ButtonStateReading ReadButtonState(QWidget *widget) {
	auto result = ButtonStateReading();
	if (!widget) {
		result.identity = u"no widget"_q;
		result.refusal = u"no widget was handed to the button-state "
			u"reading"_q;
		return result;
	}
	result.identity = WidgetDescription(widget);
	result.enabled = widget->isEnabled();
	result.visible = widget->isVisible();
	const auto button = dynamic_cast<Ui::AbstractButton*>(widget);
	if (!button) {
		result.refusal = u"not a Ui::AbstractButton, so it carries no own "
			u"disabled flag: %1"_q.arg(result.identity);
		return result;
	}
	result.disabled = button->isDisabled();
	return result;
}

QString ButtonStateDetails(const ButtonStateReading &reading) {
	const auto flag = [](bool value) {
		return value ? u"1"_q : u"0"_q;
	};
	const auto line = u"button=%1 ownDisabled=%2 isEnabled=%3 visible=%4 "
		u"usable=%5"_q.arg(
			reading.identity,
			reading.read() ? flag(reading.disabled) : u"none"_q,
			flag(reading.enabled),
			flag(reading.visible),
			flag(reading.usable()));
	return reading.read()
		? line
		: (line + u" - %1"_q.arg(reading.refusal));
}

void Click(not_null<QWidget*> widget, std::optional<QPoint> point) {
	const auto alive = base::make_weak(widget);
	const auto local = QPointF(point.value_or(widget->rect().center()));
	const auto global = QPointF(widget->mapToGlobal(local.toPoint()));
	auto press = QMouseEvent(
		QEvent::MouseButtonPress,
		local,
		global,
		Qt::LeftButton,
		Qt::LeftButton,
		Qt::NoModifier);
	if (!DeliverAndSettle(alive, press)) {
		return;
	}
	auto release = QMouseEvent(
		QEvent::MouseButtonRelease,
		local,
		global,
		Qt::LeftButton,
		Qt::NoButton,
		Qt::NoModifier);
	DeliverAndSettle(alive, release);
	DeliverPointerLeave(alive);
}

PressOrigin ClickWithPressOrigin(
		not_null<QWidget*> widget,
		std::optional<QPoint> point) {
	auto result = PressOrigin();
	result.point = point.value_or(widget->rect().center());
	result.target = WidgetDescription(widget);
	const auto refuse = [&](PressOriginRefusal refusal, QString reason) {
		result.refusal = refusal;
		result.reason = std::move(reason);
		result.targetAlive = true;
		return result;
	};
	if (widget->isWindow()) {
		return refuse(
			PressOriginRefusal::TopLevelTarget,
			u"the target is a window: moving it would move a real OS "
			u"window; nothing was moved or sent"_q);
	} else if (const auto native = FindNativeWindow(widget)) {
		return refuse(
			PressOriginRefusal::NativeTarget,
			NativeTargetReason(widget, native));
	} else if (!widget->isEnabled()) {
		return refuse(
			PressOriginRefusal::DisabledTarget,
			u"the target is disabled: QWidget::event drops its mouse "
			u"events and the press would propagate to an ancestor; "
			u"nothing was moved or sent"_q);
	} else if (!widget->rect().contains(result.point)) {
		return refuse(
			PressOriginRefusal::PointOutside,
			u"the point %1 lies outside the target's rect %2, where no "
			u"pointer press reaches this widget; nothing was moved or "
			u"sent"_q.arg(
				PointText(result.point),
				RectText(widget->rect())));
	}
	const auto alive = base::make_weak(widget);
	const auto local = QPointF(result.point);
	const auto global = QPointF(widget->mapToGlobal(result.point));
	result.pressGlobal = global.toPoint();
	auto press = QMouseEvent(
		QEvent::MouseButtonPress,
		local,
		global,
		Qt::LeftButton,
		Qt::LeftButton,
		Qt::NoModifier);
	Settle([&] {
		DeliverPressWithOrigin(widget, alive, press, result);
	});
	if (!result.pressSent || !alive.get()) {
		result.targetAlive = (alive.get() != nullptr);
		return result;
	}
	auto release = QMouseEvent(
		QEvent::MouseButtonRelease,
		local,
		global,
		Qt::LeftButton,
		Qt::NoButton,
		Qt::NoModifier);
	result.delivered = DeliverAndSettle(alive, release);
	DeliverPointerLeave(alive);
	result.targetAlive = (alive.get() != nullptr);
	return result;
}

void TypeText(not_null<QWidget*> widget, const QString &text) {
	const auto alive = base::make_weak(widget);
	auto finder = QTextBoundaryFinder(QTextBoundaryFinder::Grapheme, text);
	finder.toStart();
	auto from = 0;
	while (from < text.size()) {
		const auto till = finder.toNextBoundary();
		if (till < 0) {
			break;
		}
		const auto grapheme = text.mid(from, till - from);
		auto press = QKeyEvent(
			QEvent::KeyPress,
			0,
			Qt::NoModifier,
			grapheme);
		if (!DeliverAndSettle(alive, press)) {
			return;
		}
		auto release = QKeyEvent(
			QEvent::KeyRelease,
			0,
			Qt::NoModifier,
			grapheme);
		if (!DeliverAndSettle(alive, release)) {
			return;
		}
		from = till;
	}
}

void CommitText(not_null<QWidget*> widget, const QString &text) {
	const auto alive = base::make_weak(widget);
	auto event = QInputMethodEvent();
	event.setCommitString(text);
	DeliverAndSettle(alive, event);
}

void Drag(
		not_null<QWidget*> widget,
		QPoint from,
		QPoint to,
		int steps,
		Qt::KeyboardModifiers modifiers) {
	if (DragPressAndMoves(widget, from, to, steps, modifiers)) {
		DragRelease(widget, to, modifiers);
	}
}

bool DragPressAndMoves(
		not_null<QWidget*> widget,
		QPoint from,
		QPoint to,
		int steps,
		Qt::KeyboardModifiers modifiers) {
	const auto alive = base::make_weak(widget);
	auto press = MakeDragEvent(
		widget,
		QEvent::MouseButtonPress,
		from,
		Qt::LeftButton,
		modifiers);
	if (!DeliverAndSettle(alive, press)) {
		return false;
	}
	steps = std::max(steps, 1);
	for (auto step = 1; step <= steps; ++step) {
		const auto local = from + ((to - from) * step) / steps;
		auto move = MakeDragEvent(
			widget,
			QEvent::MouseMove,
			local,
			Qt::NoButton,
			modifiers);
		if (!DeliverAndSettle(alive, move)) {
			return false;
		}
	}
	return true;
}

void DragRelease(
		not_null<QWidget*> widget,
		QPoint to,
		Qt::KeyboardModifiers modifiers) {
	const auto alive = base::make_weak(widget);
	auto release = MakeDragEvent(
		widget,
		QEvent::MouseButtonRelease,
		to,
		Qt::LeftButton,
		modifiers);
	DeliverAndSettle(alive, release);
	DeliverPointerLeave(alive);
}

WheelDelivery Wheel(
		not_null<QWidget*> widget,
		QPoint angleDelta,
		std::optional<QPoint> point) {
	auto result = WheelDelivery();
	const auto start = WidgetDescription(widget);
	auto current = widget.get();
	auto local = QPointF(point.value_or(current->rect().center()));
	const auto global = QPointF(current->mapToGlobal(local.toPoint()));
	while (current) {
		const auto alive = base::make_weak(current);
		auto event = QWheelEvent(
			local,
			global,
			QPoint(),
			angleDelta,
			Qt::NoButton,
			Qt::NoModifier,
			Qt::NoScrollPhase,
			false,
			Qt::MouseEventSynthesizedByApplication);
		auto handled = false;
		Settle([&] {
			if (const auto strong = alive.get()) {
				handled = QApplication::sendEvent(strong, &event);
			}
		});
		const auto strong = alive.get();
		if (!strong) {
			result.refusal = u"the wheel target was destroyed during "
				u"delivery: started at %1"_q.arg(start);
			return result;
		}
		const auto identity = WidgetDescription(strong);
		if (handled && event.isAccepted()) {
			result.delivered = true;
			result.receiver = identity;
			return result;
		}
		if (!handled && event.isAccepted()) {
			result.inert = identity;
		}
		const auto isWindow = strong->isWindow();
		const auto noMousePropagation = strong->testAttribute(
			Qt::WA_NoMousePropagation);
		if (isWindow || noMousePropagation) {
			result.refusal = WheelStopRefusal(
				start,
				identity,
				result.inert,
				isWindow,
				noMousePropagation);
			return result;
		}
		local = QPointF(strong->mapToParent(local.toPoint()));
		current = strong->parentWidget();
	}
	result.refusal = result.inert.isEmpty()
		? u"no widget consumed the wheel: started at %1"_q.arg(start)
		: u"%1 returned the event still accepted without handling it; "
			u"started at %2"_q.arg(result.inert, start);
	return result;
}

TextDrop DropText(not_null<Ui::InputField*> field, const QString &text) {
	auto result = TextDrop();
	const auto alive = base::make_weak(field.get());
	const auto raw = field->rawTextEdit();
	const auto viewport = raw->viewport();
	result.target = WidgetDescription(viewport);
	result.textLength = text.size();
	result.fieldLengthBefore = field->getLastText().size();
	const auto readField = [&] {
		const auto strong = alive.get();
		result.fieldAlive = (strong != nullptr);
		result.fieldLengthAfter = strong ? strong->getLastText().size() : 0;
	};
	if (!viewport->isEnabled() || !viewport->acceptDrops()) {
		result.refusal = TextDropRefusal::ViewportRefusesDrops;
		result.reason = ViewportRefusal(viewport);
		readField();
		return result;
	}
#ifdef DESKTOP_APP_DISABLE_PRIVATE_QT_API
	result.refusal = TextDropRefusal::Unsupported;
	result.reason = u"drop emulation needs private Qt API"_q;
	readField();
	return result;
#else // DESKTOP_APP_DISABLE_PRIVATE_QT_API
	const auto viewportAlive = base::make_weak(viewport);
	const auto viewportObject = static_cast<QObject*>(viewport);
	auto mime = QMimeData();
	mime.setText(text);
	const auto point = raw->cursorRect().center();
	auto viewportDied = false;
	Settle([&] {
		auto shield = DropShield(viewport);
		auto enter = QDragEnterEvent(
			point,
			Qt::CopyAction,
			&mime,
			Qt::LeftButton,
			Qt::NoModifier);
		QApplication::sendEvent(viewport, &enter);
		result.enterAccepted = enter.isAccepted()
			&& (shield.targetEnters() > 0);
		result.shielded = shield.shielded();
		const auto strong = viewportAlive.get();
		if (!strong) {
			viewportDied = true;
			const auto manager = QDragManager::self();
			if (manager && manager->currentTarget() == viewportObject) {
				manager->setCurrentTarget(nullptr, true);
			}
		} else if (result.enterAccepted) {
			auto drop = QDropEvent(
				QPointF(point),
				Qt::CopyAction,
				&mime,
				Qt::LeftButton,
				Qt::NoModifier);
			QApplication::sendEvent(strong, &drop);
			result.dropSent = true;
			result.dropAccepted = drop.isAccepted();
		} else {
			auto leave = QDragLeaveEvent();
			QApplication::sendEvent(strong, &leave);
		}
	});
	readField();
	if (viewportDied) {
		result.refusal = TextDropRefusal::DropNotAccepted;
		result.reason = u"the editor viewport was destroyed while handling "
			u"the enter; no drop was sent"_q;
	} else if (!result.enterAccepted) {
		result.refusal = TextDropRefusal::EnterNotAccepted;
		result.reason = ShieldedRefusal(result.shielded);
	} else if (!result.dropAccepted) {
		result.refusal = TextDropRefusal::DropNotAccepted;
		result.reason = u"the field's editor accepted the enter but did not "
			u"accept the drop"_q;
	} else {
		result.delivered = true;
	}
	return result;
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API
}

void PressKey(
		not_null<QWidget*> widget,
		int key,
		Qt::KeyboardModifiers modifiers) {
	const auto alive = base::make_weak(widget);
	auto press = QKeyEvent(QEvent::KeyPress, key, modifiers);
	if (!DeliverAndSettle(alive, press)) {
		return;
	}
	auto release = QKeyEvent(QEvent::KeyRelease, key, modifiers);
	DeliverAndSettle(alive, release);
}

void Settle(Fn<void()> action) {
	auto &sandbox = Core::Sandbox::Instance();
	sandbox.setPostponedCallsDeferred(true);
	{
		const auto guard = gsl::finally([&] {
			sandbox.setPostponedCallsDeferred(false);
		});
		action();
	}
	SettlePostponedCalls();
}

void SettlePostponedCalls() {
	Core::Sandbox::Instance().drainPostponedCalls();
}

WindowActivation ReadWindowActivation(QWidget *widget) {
	const auto window = ResolveActivationWindow(widget);
	auto result = WindowActivation();
	result.focusWindowSet = (QGuiApplication::focusWindow() != nullptr);
	result.activeWindow = (QApplication::activeWindow() != nullptr);
	result.attempts = ActivationAttempts();
	result.notes = ActivationNotes();
	result.identity = widget
		? WidgetDescription(widget)
		: ActivationWindowIdentity(window);
	if (window) {
		return result;
	} else if (!widget) {
		result.refusal = u"no visible top-level QWindow to activate"_q;
		return result;
	}
	result.refusal = u"the widget's own window carries no QWindow handle, "
		u"so there is nothing to activate through: %1"_q
		.arg(result.identity);
	return result;
}

WindowActivation ForceWindowActive(QWidget *widget) {
	++ActivationAttempts();
	const auto window = ResolveActivationWindow(widget);
	if (!window) {
		return ReadWindowActivation(widget);
	}
	InjectActivation(window);
	auto result = ReadWindowActivation(widget);
	result.injected = true;
	result.refusal = QString();
	if (((result.attempts - 1) % kActivationNoteEvery) == 0) {
		++ActivationNotes();
		result.notes = ActivationNotes();
		Note(WindowActivationDetails(result));
	}
	return result;
}

WindowActivation ClearWindowActive() {
	InjectActivation(nullptr);
	auto result = ReadWindowActivation(nullptr);
	result.injected = true;
	result.refusal = QString();
	result.identity = u"none - the application focus window was cleared"_q;
	Note(WindowActivationDetails(result));
	return result;
}

QString WheelDeliveryDetails(const WheelDelivery &reading) {
	const auto line = u"wheel: delivered=%1 receiver=%2 inert=%3"_q
		.arg(reading.delivered ? 1 : 0)
		.arg(reading.receiver.isEmpty() ? u"none"_q : reading.receiver)
		.arg(reading.inert.isEmpty() ? u"none"_q : reading.inert);
	return reading.refusal.isEmpty()
		? line
		: (line + u" - %1"_q.arg(reading.refusal));
}

QString TextDropRefusalName(TextDropRefusal refusal) {
	switch (refusal) {
	case TextDropRefusal::None: return u"none"_q;
	case TextDropRefusal::ViewportRefusesDrops:
		return u"viewport-refuses-drops"_q;
	case TextDropRefusal::EnterNotAccepted: return u"enter-not-accepted"_q;
	case TextDropRefusal::DropNotAccepted: return u"drop-not-accepted"_q;
	case TextDropRefusal::Unsupported: return u"unsupported"_q;
	}
	Unexpected("Refusal in Test::TextDropRefusalName.");
}

QString TextDropDetails(const TextDrop &reading) {
	const auto line = u"text drop: delivered=%1 refusal=%2 enterAccepted=%3 "
		u"dropSent=%4 dropAccepted=%5 fieldAlive=%6 textLength=%7 "
		u"fieldLength=%8->%9 target=%10 shielded=[%11]"_q
		.arg(reading.delivered ? 1 : 0)
		.arg(TextDropRefusalName(reading.refusal))
		.arg(reading.enterAccepted ? 1 : 0)
		.arg(reading.dropSent ? 1 : 0)
		.arg(reading.dropAccepted ? 1 : 0)
		.arg(reading.fieldAlive ? 1 : 0)
		.arg(reading.textLength)
		.arg(reading.fieldLengthBefore)
		.arg(reading.fieldLengthAfter)
		.arg(reading.target)
		.arg(reading.shielded.join(u"; "_q));
	return reading.reason.isEmpty()
		? line
		: (line + u" - %1"_q.arg(reading.reason));
}

QString PressOriginRefusalName(PressOriginRefusal refusal) {
	switch (refusal) {
	case PressOriginRefusal::None: return u"none"_q;
	case PressOriginRefusal::TopLevelTarget: return u"top-level-target"_q;
	case PressOriginRefusal::NativeTarget: return u"native-target"_q;
	case PressOriginRefusal::DisabledTarget: return u"disabled-target"_q;
	case PressOriginRefusal::PointOutside: return u"point-outside"_q;
	case PressOriginRefusal::OriginUnreachable:
		return u"origin-unreachable"_q;
	}
	Unexpected("Refusal in Test::PressOriginRefusalName.");
}

QString PressOriginDetails(const PressOrigin &reading) {
	const auto line = u"press origin: delivered=%1 refusal=%2 pressSent=%3 "
		u"displaced=%4 restored=%5 pressAccepted=%6 targetAlive=%7 "
		u"cursorStable=%8 geometryChangedByPress=%9 point=%10 "
		u"cursor=%11->%12 displacement=%13 originSeen=%14 pressGlobal=%15 "
		u"moveEventsWithheld=%16 paintsWhileDisplaced=%17 target=%18"_q
		.arg(reading.delivered ? 1 : 0)
		.arg(PressOriginRefusalName(reading.refusal))
		.arg(reading.pressSent ? 1 : 0)
		.arg(reading.displaced ? 1 : 0)
		.arg(reading.restored ? 1 : 0)
		.arg(reading.pressAccepted ? 1 : 0)
		.arg(reading.targetAlive ? 1 : 0)
		.arg(reading.cursorStable ? 1 : 0)
		.arg(reading.geometryChangedByPress ? 1 : 0)
		.arg(PointText(reading.point))
		.arg(PointText(reading.cursorBefore))
		.arg(PointText(reading.cursorAfter))
		.arg(PointText(reading.displacement))
		.arg(PointText(reading.originSeen))
		.arg(PointText(reading.pressGlobal))
		.arg(reading.moveEventsWithheld)
		.arg(reading.paintsWhileDisplaced)
		.arg(reading.target);
	return reading.reason.isEmpty()
		? line
		: (line + u" - %1"_q.arg(reading.reason));
}

QString WindowActivationDetails(const WindowActivation &reading) {
	const auto line = u"window activation: injected=%1 focusWindowSet=%2 "
		u"activeWindow=%3 attempts=%4 notes=%5 target=%6"_q
		.arg(reading.injected ? 1 : 0)
		.arg(reading.focusWindowSet ? 1 : 0)
		.arg(reading.activeWindow ? 1 : 0)
		.arg(reading.attempts)
		.arg(reading.notes)
		.arg(reading.identity);
	return reading.refusal.isEmpty()
		? line
		: (line + u" - %1"_q.arg(reading.refusal));
}

} // namespace Test

#endif // _DEBUG
